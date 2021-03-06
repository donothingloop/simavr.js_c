#include <stdlib.h>
#include <stdio.h>

#include <sim_avr.h>
#include <sim_hex.h>
#include <avr_ioport.h>
#include <sim_elf.h>
#include <memory.h>
#include "simulator.h"

#include "parts/wdg0151.h"

avr_t *avr = NULL;
wdg0151_t glcd;

#define AVR_FREQ 16000000L

#ifdef EMSCRIPTEN
#include <emscripten.h>
#else
void print_bits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = b[i] & (1<<j);
            byte >>= j;
            printf("%u", byte);
        }
    }
    puts("");
}
#endif

static void pin_changed_hook(struct avr_irq_t *irq, uint32_t value, void *param) {
#ifdef EMSCRIPTEN
    EM_ASM_({
            port_changed($0, $1);
            }, *((char*)param), value);
#else
    printf("pin_changed: %c, ", *((char*)param));
    print_bits(sizeof(uint8_t), &value);
#endif
}

#ifdef EMSCRIPTEN
EMSCRIPTEN_KEEPALIVE
void set_pin(char port, uint16_t pin, uint8_t value) {
    printf("set_pin: Port %c, pin: %u, value: %u\n", port, pin, value);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(port), pin), value);
}

EMSCRIPTEN_KEEPALIVE
char *get_ports() {
    return ports;
}

EMSCRIPTEN_KEEPALIVE
int run() {
    int state;
    for(int i = 0; i < 5000; i++) {
        state = avr_run(avr);

        if(state == cpu_Done || state == cpu_Crashed)
            return state;
    }

    return -1;
}
#endif

#ifndef EMSCRIPTEN
void loop() {
    for (; ;) {
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed)
            break;
    }

    printf("i'll be back\n");
}

#endif

static void glcd_callback() {
#ifdef EMSCRIPTEN
    char data[WDG0151_WIDTH*WDG0151_HEIGHT+1];
    data[WDG0151_WIDTH*WDG0151_HEIGHT] = 0;
    
    int i = 0;
    for(int y = 0; y < WDG0151_HEIGHT; y++) {
        for(int x = 0; x < WDG0151_WIDTH; x++) {
            data[i++] = (char)glcd.ctrl1.data[y][x];
        }
    }    

    for(int y = 0; y < WDG0151_HEIGHT; y++) {
        for(int x = 0; x < WDG0151_WIDTH; x++) {
            data[i++] = (char)glcd.ctrl2.data[y][x];
        }
    }    

    EM_ASM_({
        glcd_data($0);
    }, data);
#else
    wdg0151_print(&glcd);
#endif
}

static void load_file(char *filename) {
    elf_firmware_t f;
    ihex_chunk_p chunk = NULL;
   
    int cnt = read_ihex_chunks(filename, &chunk);
    uint32_t loadBase = AVR_SEGMENT_OFFSET_FLASH;

    if (cnt <= 0) {
        fprintf(stderr, "Unable to load IHEX file %s\n",
                filename);
        exit(1);
    }

    printf("Loaded %d section of ihex\n", cnt);
    for (int ci = 0; ci < cnt; ci++) {
        if (chunk[ci].baseaddr < (1 * 1024 * 1024)) {
            f.flash = chunk[ci].data;
            f.flashsize = chunk[ci].size;
            f.flashbase = chunk[ci].baseaddr;
            printf("Load HEX flash %08x, %d\n", f.flashbase, f.flashsize);
        } else if (chunk[ci].baseaddr >= AVR_SEGMENT_OFFSET_EEPROM ||
                chunk[ci].baseaddr + loadBase >= AVR_SEGMENT_OFFSET_EEPROM) {
            // TODO: implement eeprom loading
            // eeprom!
            f.eeprom = chunk[ci].data;
            f.eesize = chunk[ci].size;
            printf("Load HEX eeprom %08x, %d\n", chunk[ci].baseaddr, f.eesize);
        }
    }

    f.frequency = AVR_FREQ;
    strcpy(f.mmcu, "atmega1280");
    printf("firmware %s f=%d mmcu=%s\n", filename, (int) f.frequency, f.mmcu);

    avr = avr_make_mcu_by_name(f.mmcu);
    if (!avr) {
        fprintf(stderr, "AVR '%s' not known\n", f.mmcu);
        exit(1);
    }

    avr_init(avr);
    avr->frequency = AVR_FREQ;

    avr_load_firmware(avr, &f);

    avr->log = LOG_TRACE;
#ifndef EMSCRIPTEN
    avr->trace = 1;
#endif

    printf("registering port callbacks:");
    for (unsigned pi = 0; pi < (sizeof(ports) / sizeof(ports[0])) -1; pi++) {
        printf(" %c", ports[pi]);
        avr_irq_t *irq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(ports[pi]), IOPORT_IRQ_PIN_ALL);
        avr_irq_register_notify(irq, pin_changed_hook, &(ports[pi]));
    }

    printf("\n");

    wdg0151_init(avr, &glcd);
    glcd.cb = &glcd_callback;

    for(int i = 0; i < 8; i++) {
        avr_irq_t *iavr = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), i);
        avr_irq_t *ilcd = glcd.irq + IRQ_WDG0151_D0 + i;

        avr_connect_irq(iavr, ilcd);
        avr_connect_irq(ilcd, iavr);
    }

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 2),
            glcd.irq + IRQ_WDG0151_CS1);

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 3),
            glcd.irq + IRQ_WDG0151_CS2);

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 4),
            glcd.irq + IRQ_WDG0151_RS);

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 5),
            glcd.irq + IRQ_WDG0151_RW);

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 6),
            glcd.irq + IRQ_WDG0151_E);

    avr_connect_irq(
            avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('E'), 7),
            glcd.irq + IRQ_WDG0151_RST);

}

#ifdef EMSCRIPTEN
    EMSCRIPTEN_KEEPALIVE
void init(char* filename)
{
    load_file(filename);
}
#else
void init(char* filename) {
    load_file(filename);
}
#endif

#ifndef EMSCRIPTEN
void usage(char* cmd) {
    printf("Usage: %s [hex-file]\nsimavr.js test tool\n", cmd);
    exit(1);
}

int main(int argc, char **argv) {
    if(argc < 2)
        usage(argv[0]);

    init(argv[1]);
    loop();
    return 0;
}
#endif
