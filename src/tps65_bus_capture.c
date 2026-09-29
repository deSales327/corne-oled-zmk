/* SPDX-License-Identifier: MIT */
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <soc.h>

LOG_MODULE_REGISTER(tps65_bus_capture, LOG_LEVEL_INF);

/* Read-only 1 ms samples expose long stalls, not individual I2C bits.
 * No logging, peripheral writes, or I2C calls occur in the sampling ISR. */
BUILD_ASSERT(CONFIG_INPUT_INIT_PRIORITY > 89, "Capture must precede TPS65 init");
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(i2c1)) == NRF_TWI1_BASE,
             "Capture requires the existing TWI1 controller");

struct bus_sample {
    uint32_t ms, enable, pins, scl_cfg, sda_cfg;
    uint32_t psel_scl, psel_sda, frequency, address;
    uint32_t event_bits, errorsrc, inten, irq_enabled, irq_pending;
};

static struct bus_sample first_active, last_active, first_low, after_active;
static uint32_t samples, active_samples, scl_connected, sda_connected;
static uint32_t scl_low, sda_low, both_high, irq_pending_samples;
static bool have_active, have_low, have_after;
static atomic_t finished;

static struct bus_sample sample_bus(void) {
    uint32_t p0 = NRF_P0->IN;
    uint32_t p1 = NRF_P1->IN;
    IRQn_Type irq = (IRQn_Type)DT_IRQN(DT_NODELABEL(i2c1));
    return (struct bus_sample) {
        .ms = k_uptime_get_32(),
        .enable = NRF_TWI1->ENABLE,
        /* bits 0 SDA, 1 SCL, 2 RDY, 3 NRST output latch */
        .pins = ((p0 >> 10) & 1U) | (((p0 >> 9) & 1U) << 1) |
                (((p1 >> 6) & 1U) << 2) | (((NRF_P1->OUT >> 4) & 1U) << 3),
        .scl_cfg = NRF_P0->PIN_CNF[9],
        .sda_cfg = NRF_P0->PIN_CNF[10],
        .psel_scl = NRF_TWI1->PSEL.SCL,
        .psel_sda = NRF_TWI1->PSEL.SDA,
        .frequency = NRF_TWI1->FREQUENCY,
        .address = NRF_TWI1->ADDRESS,
        /* bits 0 STOPPED, 1 RXDREADY, 2 TXDSENT, 3 ERROR, 4 BB, 5 SUSPENDED */
        .event_bits = (NRF_TWI1->EVENTS_STOPPED ? 1U : 0U) |
                      (NRF_TWI1->EVENTS_RXDREADY ? 2U : 0U) |
                      (NRF_TWI1->EVENTS_TXDSENT ? 4U : 0U) |
                      (NRF_TWI1->EVENTS_ERROR ? 8U : 0U) |
                      (NRF_TWI1->EVENTS_BB ? 16U : 0U) |
                      (NRF_TWI1->EVENTS_SUSPENDED ? 32U : 0U),
        .errorsrc = NRF_TWI1->ERRORSRC,
        .inten = NRF_TWI1->INTENSET,
        .irq_enabled = NVIC_GetEnableIRQ(irq),
        .irq_pending = NVIC_GetPendingIRQ(irq),
    };
}

static void capture_tick(struct k_timer *timer) {
    struct bus_sample s = sample_bus();
    samples++;
    if (s.enable == 5U) { /* nRF52840 TWI ENABLE: Enabled = 5 */
        if (!have_active) {
            first_active = s;
            have_active = true;
        }
        last_active = s;
        active_samples++;
        bool scl_valid = !(s.scl_cfg & GPIO_PIN_CNF_INPUT_Msk);
        bool sda_valid = !(s.sda_cfg & GPIO_PIN_CNF_INPUT_Msk);
        scl_connected += scl_valid;
        sda_connected += sda_valid;
        bool low_clock = scl_valid && !(s.pins & 2U);
        bool low_data = sda_valid && !(s.pins & 1U);
        scl_low += low_clock;
        sda_low += low_data;
        both_high += scl_valid && sda_valid && ((s.pins & 3U) == 3U);
        irq_pending_samples += !!s.irq_pending;
        if (!have_low && (low_clock || low_data)) {
            first_low = s;
            have_low = true;
        }
    } else if (have_active && !have_after) {
        after_active = s;
        have_after = true;
    }
    if (samples >= 2000U) {
        k_timer_stop(timer);
        atomic_set(&finished, 1);
    }
}

K_TIMER_DEFINE(bus_timer, capture_tick, NULL);

static int start_bus_capture(void) {
    k_timer_start(&bus_timer, K_MSEC(1), K_MSEC(1));
    return 0;
}
SYS_INIT(start_bus_capture, POST_KERNEL, 89);

static void print_sample(const char *name, const struct bus_sample *s) {
    LOG_INF("BUS89 %s ms=%u enable=%u pins=0x%x cfg_scl=0x%x cfg_sda=0x%x",
            name, s->ms, s->enable, s->pins, s->scl_cfg, s->sda_cfg);
    LOG_INF("BUS89 %s psel_scl=0x%x psel_sda=0x%x freq=0x%x addr=0x%x",
            name, s->psel_scl, s->psel_sda, s->frequency, s->address);
    LOG_INF("BUS89 %s events=0x%x errorsrc=0x%x inten=0x%x irq_en=%u irq_pending=%u",
            name, s->event_bits, s->errorsrc, s->inten, s->irq_enabled, s->irq_pending);
}

void tps65_bus_capture_report(void) {
    /* Timer stopped: captured data is immutable once this flag is set. */
    if (!atomic_get(&finished)) {
        LOG_INF("BUS89 capture pending");
        return;
    }
    LOG_INF("BUS89 samples=%u active=%u scl_valid=%u sda_valid=%u scl_low=%u sda_low=%u both_high=%u irq_pending=%u",
            samples, active_samples, scl_connected, sda_connected,
            scl_low, sda_low, both_high, irq_pending_samples);
    if (have_active) {
        print_sample("first", &first_active);
        print_sample("last", &last_active);
    }
    if (have_low) {
        print_sample("low", &first_low);
    }
    if (have_after) {
        print_sample("after", &after_active);
    }
}
