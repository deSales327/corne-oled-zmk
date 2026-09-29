/* SPDX-License-Identifier: MIT */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <soc.h>

LOG_MODULE_REGISTER(tps65_probe, LOG_LEVEL_INF);

static atomic_t events;
static atomic_t x_events;
static atomic_t y_events;
static atomic_t button_events;
static atomic_t last_x;
static atomic_t last_y;

/* Observe only TPS65 input events. No keyboard key events are recorded. */
static void observe_tps65(struct input_event *event) {
    atomic_inc(&events);
    if (event->type == INPUT_EV_REL && event->code == INPUT_REL_X) {
        atomic_inc(&x_events);
        atomic_set(&last_x, event->value);
    } else if (event->type == INPUT_EV_REL && event->code == INPUT_REL_Y) {
        atomic_inc(&y_events);
        atomic_set(&last_y, event->value);
    } else if (event->type == INPUT_EV_KEY) {
        atomic_inc(&button_events);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(tps65)), observe_tps65);

static struct k_work_delayable report_work;
void tps65_bus_capture_report(void);

static void report_status(struct k_work *work) {
    ARG_UNUSED(work);
    LOG_INF("TPS65_PROBE base=59 ready=%d events=%d x_events=%d y_events=%d buttons=%d last_x=%d last_y=%d",
            device_is_ready(DEVICE_DT_GET(DT_NODELABEL(tps65))),
            (int)atomic_get(&events), (int)atomic_get(&x_events),
            (int)atomic_get(&y_events), (int)atomic_get(&button_events),
            (int)atomic_get(&last_x), (int)atomic_get(&last_y));
    /* Register reads only: do not reconfigure pins or access I2C from the probe.
     * IN values are instantaneous samples, NOT voltage or waveform readings.
     * An input-disconnected PIN_CNF can make IN unsuitable for pin diagnosis.
     * OUT values are output latches, not measurements of physical voltage.
     */
    LOG_INF("TPS65_PINS SDA_in=%u SCL_in=%u RDY_in=%u NRST_out=%u VCC_ctrl_out=%u NFCPINS=0x%08x",
            (unsigned)((NRF_P0->IN >> 10) & 1U),
            (unsigned)((NRF_P0->IN >> 9) & 1U),
            (unsigned)((NRF_P1->IN >> 6) & 1U),
            (unsigned)((NRF_P1->OUT >> 4) & 1U),
            (unsigned)((NRF_P0->OUT >> 13) & 1U),
            (unsigned)NRF_UICR->NFCPINS);
    LOG_INF("TPS65_PINCFG SDA=0x%08x SCL=0x%08x RDY=0x%08x",
            (unsigned)NRF_P0->PIN_CNF[10], (unsigned)NRF_P0->PIN_CNF[9],
            (unsigned)NRF_P1->PIN_CNF[6]);
    tps65_bus_capture_report();
    k_work_schedule(&report_work, K_SECONDS(5));
}

static int start_probe(void) {
    k_work_init_delayable(&report_work, report_status);
    k_work_schedule(&report_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(start_probe, APPLICATION, 99);
