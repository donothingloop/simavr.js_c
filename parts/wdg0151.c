#include "wdg0151.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sim_time.h>

void wdg0151_print(struct wdg0151_t *wdg) {
    for(uint8_t y = 0; y < WDG0151_HEIGHT; y++) {
        for(uint8_t x = 0; x < WDG0151_WIDTH; x++) {
            printf("%u ", wdg->data[y][x]);
        }

        printf("\n");
    }
}

static const char* irq_names[IRQ_WDG0151_COUNT] = {
    [IRQ_WDG0151_CS1] = "<wdg0151.cs1",
    [IRQ_WDG0151_CS2] = "<wdg0151.cs2",
    [IRQ_WDG0151_RS] = "<wdg0151.rs",
    [IRQ_WDG0151_RW] = "<wdg0151.rw",
    [IRQ_WDG0151_E] = "<wdg0151.e",
    [IRQ_WDG0151_D0] = "=wdg0151.d0",
    [IRQ_WDG0151_D1] = "=wdg0151.d1",
    [IRQ_WDG0151_D2] = "=wdg0151.d2",
    [IRQ_WDG0151_D3] = "=wdg0151.d3",
    [IRQ_WDG0151_D4] = "=wdg0151.d4",
    [IRQ_WDG0151_D5] = "=wdg0151.d5",
    [IRQ_WDG0151_D6] = "=wdg0151.d6",
    [IRQ_WDG0151_D7] = "=wdg0151.d7",
    [IRQ_WDG0151_RST] = "<wdg0151.rst",
};

static int wdg0151_get_cs(struct wdg0151_t *wdg) {
    if(!(wdg->pinstate & (1<<IRQ_WDG0151_CS2)))
        return 2;
    else
        return 1;
}

static struct wdg0151_ctrl_t* wdg0151_get_ctrl(struct wdg0151_t *wdg) {
    int ctrl = wdg0151_get_cs(wdg);

    if(ctrl == 1) {
        return &wdg->ctrl1;
    } else {
        return &wdg->ctrl2;
    }
}

static avr_cycle_count_t wdg0151_busy_timer(struct avr_t *avr, avr_cycle_count_t when, void *param) {
    struct wdg0151_t *wdg = (struct wdg0151_t*) param;
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 

    ctrl->busy = false;
    return 0;
}

static uint32_t wdg0151_write_data(struct wdg0151_t *wdg) {
    uint32_t delay = 200; // TODO
    uint8_t shift = 0;
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 
    printf("glcd: write_data (%u): data: %u, x: %u, y: %u\n", wdg0151_get_cs(wdg), wdg->datapins, ctrl->x_addr, ctrl->y_addr);

    if(!(wdg->pinstate & (1<<IRQ_WDG0151_CS2)))
        shift = 64;
    
    wdg->data[ctrl->y_addr][ctrl->x_addr + shift] = wdg->datapins;
    ctrl->x_addr++;

    if(ctrl->x_addr >= 128)
        ctrl->x_addr = 0;

    if(wdg->cb)
        wdg->cb();

    return delay;
}

static uint32_t wdg0151_write_instruction(struct wdg0151_t *wdg) {
    uint32_t delay = 200; // TODO
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 
    
    if((wdg->pinstate & (1<<IRQ_WDG0151_D6)) && !(wdg->pinstate & (1<<IRQ_WDG0151_D7))) {
        // Y address
        uint8_t y = wdg->datapins & 0x3F;
        printf("glcd: write_instruction: y_addr: %u\n", y);
        ctrl->x_addr = y;
    } else if((wdg->pinstate & (1<<IRQ_WDG0151_D7)) && (wdg->pinstate & (1<<IRQ_WDG0151_D6))) {
        // start line
        uint8_t start = wdg->datapins & 0x3F;
        printf("glcd: write_instruction: start: %u\n", start);
        ctrl->start = start;
        printf("glcd: Using unsupported feature startline!\n");
    } else if((wdg->pinstate & (1<<IRQ_WDG0151_D7)) && !(wdg->pinstate & (1<<IRQ_WDG0151_D6))) {
        // X address
        uint8_t x = wdg->datapins & 0x7;
        printf("glcd: write_instruction: x_addr: %u\n", x);
        ctrl->y_addr = x;
    } else if(!(wdg->pinstate & (1<<IRQ_WDG0151_D7)) && !(wdg->pinstate & (1<<IRQ_WDG0151_D6))) {
        // on/off
        if(wdg->pinstate & (1<<IRQ_WDG0151_D0)) {
            printf("glcd: write_instruction: on\n");
            ctrl->enabled = true;
        } else {
            printf("glcd: write_instruction: off\n");
            ctrl->enabled = false;
        }
    }

    return delay;
}

static uint32_t wdg0151_process_write(struct wdg0151_t *wdg) {
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 
    wdg->datapins = ((wdg->pinstate>>IRQ_WDG0151_D0) & 0xFF);
    ctrl->dummy = 1;

    int delay = 0;
    if(wdg->pinstate & (1<<IRQ_WDG0151_RS))
        delay = wdg0151_write_data(wdg);
    else
        delay = wdg0151_write_instruction(wdg); 

    return delay;
}

static void wdg0151_write_datapins(struct wdg0151_t *wdg, uint8_t data) {
    for(int i = 0; i < 8; i++) {
        avr_raise_irq(wdg->irq + IRQ_WDG0151_D0 + i, (data<<i) & 1);
    }

    wdg->datapins = data;
}

static uint32_t wdg0151_process_read(struct wdg0151_t *wdg) {
    uint32_t delay = 500;
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 

    if(wdg->pinstate & (1<<IRQ_WDG0151_RS)) {
        uint8_t shift = 0;

        if(!(wdg->pinstate & (1<<IRQ_WDG0151_CS2)))
            shift = 64;
 
        if(ctrl->dummy) {
            printf("glcd: read_data: dummy read\n");
            ctrl->dummy--;
        } else {
            printf("glcd: read_data: %u\n", wdg->data[ctrl->y_addr][ctrl->x_addr + shift]);
            wdg0151_write_datapins(wdg, wdg->data[ctrl->y_addr][ctrl->x_addr + shift]);
            ctrl->x_addr++;

            if(ctrl->x_addr >= 128)
                ctrl->x_addr = 0;
        }
       
    } else {
        printf("glcd: read_status (%u): reset: %u, enabled: %u, busy:%u\n", wdg0151_get_cs(wdg), ctrl->reset, ctrl->enabled, ctrl->busy); 
        uint8_t data = 0 | (ctrl->reset<<4) | (ctrl->enabled<<5) | (ctrl->busy<<7);
        wdg0151_write_datapins(wdg, data);
    }

    return delay;
}

static avr_cycle_count_t wdg0151_process_e_pinchange(struct avr_t *avr, avr_cycle_count_t when, void *param) {
    struct wdg0151_t *wdg = (struct wdg0151_t*) param;
    struct wdg0151_ctrl_t *ctrl = wdg0151_get_ctrl(wdg); 

    ctrl->reentrant = true;

    int delay = 0;
    
    if(!(wdg->pinstate & (1<<IRQ_WDG0151_RW)))
        delay = wdg0151_process_write(wdg);
    else
        delay = wdg0151_process_read(wdg);

    if(delay) {
        ctrl->busy = true;
        avr_cycle_timer_register_usec(wdg->avr, delay, wdg0151_busy_timer, wdg);
    }

    ctrl->reentrant = false;    

    return delay;
}

static void wdg0151_pin_changed(struct avr_irq_t *irq, uint32_t value, void *param) {
    struct wdg0151_t *wdg = (struct wdg0151_t*) param;
    uint16_t old = wdg->pinstate;

    // check if currently reading
//    if(wdg->reentrant)
//        return;

    wdg->pinstate = (wdg->pinstate & ~(1<<irq->irq)) | (value<<irq->irq);

    int eo = old & (1<<IRQ_WDG0151_E);
    int e = wdg->pinstate & (1<<IRQ_WDG0151_E);

    if(!eo && e)
        wdg0151_process_e_pinchange(wdg->avr, 0, wdg);
        //avr_cycle_timer_register(wdg->avr, 1, wdg0151_process_e_pinchange, wdg);
}

void wdg0151_init(struct avr_t *avr, struct wdg0151_t *wdg) {
    memset(wdg, 0, sizeof(*wdg));
    wdg->avr = avr;

    wdg->irq = avr_alloc_irq(&avr->irq_pool, 0, IRQ_WDG0151_COUNT, irq_names);

    for(int i = 0; i < IRQ_WDG0151_COUNT; i++)
       avr_irq_register_notify(wdg->irq + i, wdg0151_pin_changed, wdg);
}
