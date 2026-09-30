/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tps65_gpio_test, LOG_LEVEL_INF);

static const struct gpio_dt_spec sda = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 10,
    .dt_flags = GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN,
};
static const struct gpio_dt_spec scl = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 9,
    .dt_flags = GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN,
};
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET(DT_NODELABEL(tps65), reset_gpios);
static const struct gpio_dt_spec rdy = GPIO_DT_SPEC_GET(DT_NODELABEL(tps65), rdy_gpios);

BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay), "Hardware I2C1 must be disabled");
BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(tps65), okay), "Sensor driver must be disabled");

static int64_t transfer_deadline;
static int64_t reset_released_ms, first_ready_ms = -1;
static unsigned ack_count, ack_mask;
static unsigned stage;
static int levels[4], rdy_reset = -1, rdy_ready = -1;
static uint16_t product;
static bool product_valid;
#define TRY(expr) do { int result_ = (expr); if (result_ < 0) { return result_; } } while (0)

/* Slow diagnostic I2C; both lines are open drain, never driven high.
 * Every raised SCL waits for its physical input, with a 500 ms stretch limit
 * and a 2500 ms total transaction deadline. IRQs remain enabled. */
static void half_period(void) { k_busy_wait(50); }

static int raise_scl(void) {
    TRY(gpio_pin_set_dt(&scl, 1));
    int64_t until = k_uptime_get() + 500;
    for (;;) {
        if (k_uptime_get() >= transfer_deadline) { return -ETIMEDOUT; }
        int high = gpio_pin_get_dt(&scl);
        if (high < 0) { return high; }
        if (high) { return 0; }
        if (k_uptime_get() >= until) { return -ETIMEDOUT; }
        k_usleep(50);
    }
}

static int start_condition(void) {
    TRY(gpio_pin_set_dt(&sda, 1));
    half_period();
    TRY(raise_scl());
    half_period();
    int high = gpio_pin_get_dt(&sda);
    if (high < 0) { return high; }
    if (!high) { return -EBUSY; }
    TRY(gpio_pin_set_dt(&sda, 0));
    half_period();
    TRY(gpio_pin_set_dt(&scl, 0));
    half_period();
    return 0;
}

static int stop_condition(void) {
    TRY(gpio_pin_set_dt(&scl, 0));
    TRY(gpio_pin_set_dt(&sda, 0));
    half_period();
    TRY(raise_scl());
    half_period();
    TRY(gpio_pin_set_dt(&sda, 1));
    half_period();
    return 0;
}

static int write_bit(int value) {
    TRY(gpio_pin_set_dt(&sda, value));
    half_period();
    TRY(raise_scl());
    half_period();
    TRY(gpio_pin_set_dt(&scl, 0));
    half_period();
    return 0;
}

static int read_bit(int *value) {
    TRY(gpio_pin_set_dt(&sda, 1));
    half_period();
    TRY(raise_scl());
    half_period();
    int bit = gpio_pin_get_dt(&sda);
    TRY(gpio_pin_set_dt(&scl, 0));
    half_period();
    if (bit < 0) { return bit; }
    *value = !!bit;
    return 0;
}

static int write_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; bit--) { TRY(write_bit((value >> bit) & 1)); }
    int nack;
    TRY(read_bit(&nack));
    if (ack_count < 32 && !nack) { ack_mask |= (1U << ack_count); }
    ack_count++;
    return nack ? -ENXIO : 0;
}

static int read_byte(uint8_t *value, bool last) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        int bit;
        TRY(read_bit(&bit));
        byte = (byte << 1) | bit;
    }
    TRY(write_bit(last ? 1 : 0));
    *value = byte;
    return 0;
}

static int line_levels(void) {
    int a = gpio_pin_get_dt(&sda), c = gpio_pin_get_dt(&scl);
    if (a < 0) { return a; }
    if (c < 0) { return c; }
    return (!!a) | ((!!c) << 1); /* bit0 SDA, bit1 SCL */
}

static int identify(void) {
    uint8_t hi, lo;
    transfer_deadline = k_uptime_get() + 2500;
    stage = 10; TRY(start_condition());
    stage = 11; TRY(write_byte(0x74 << 1));
    stage = 12; TRY(write_byte(0x00)); /* Product number register high address */
    stage = 13; TRY(write_byte(0x00)); /* Product number register low address */
    stage = 14; TRY(start_condition()); /* repeated START */
    stage = 15; TRY(write_byte((0x74 << 1) | 1));
    stage = 16; TRY(read_byte(&hi, false));
    stage = 17; TRY(read_byte(&lo, true));
    stage = 18; TRY(stop_condition());
    product = ((uint16_t)hi << 8) | lo;
    product_valid = true;

    /* B000 communication window ends with a write to 0xEEEE. */
    stage = 19; TRY(start_condition());
    stage = 20; TRY(write_byte(0x74 << 1));
    stage = 21; TRY(write_byte(0xEE));
    stage = 22; TRY(write_byte(0xEE));
    stage = 23; TRY(write_byte(0x00));
    stage = 24; TRY(stop_condition());
    stage = 25;
    return 0;
}

/* Continue only the RDY wait. A failed line test or I2C transaction is not retried. */
static int monitor_step(int result) {
    if (stage != 4 || result != -ETIMEDOUT) { return result; }
    rdy_ready = gpio_pin_get_dt(&rdy);
    if (rdy_ready < 0) { stage = 5; return rdy_ready; }
    if (!rdy_ready) { return result; }
    first_ready_ms = k_uptime_get() - reset_released_ms;
    return identify();
}

static void release_lines(int result) {
    if (stage >= 10 && result < 0) {
        transfer_deadline = k_uptime_get() + 500;
        (void)stop_condition();
    }
    (void)gpio_pin_set_dt(&sda, 1);
    (void)gpio_pin_set_dt(&scl, 1);
    (void)gpio_pin_set_dt(&rst, 0);
}

static int run_test(void) {
    stage = 1;
    if (!gpio_is_ready_dt(&sda) || !gpio_is_ready_dt(&scl) ||
        !gpio_is_ready_dt(&rst) || !gpio_is_ready_dt(&rdy)) { return -ENODEV; }
    TRY(gpio_pin_configure_dt(&rdy, GPIO_INPUT));
    TRY(gpio_pin_configure_dt(&rst, GPIO_OUTPUT_ACTIVE));
    TRY(gpio_pin_configure_dt(&sda, GPIO_INPUT | GPIO_OUTPUT_HIGH));
    TRY(gpio_pin_configure_dt(&scl, GPIO_INPUT | GPIO_OUTPUT_HIGH));
    k_msleep(100);
    rdy_reset = gpio_pin_get_dt(&rdy);

    /* Check independent line control while the sensor is held in reset. */
    stage = 2; levels[0] = line_levels();
    if (levels[0] != 3) { return -EIO; }
    TRY(gpio_pin_set_dt(&scl, 0)); k_busy_wait(100);
    levels[1] = line_levels();
    TRY(gpio_pin_set_dt(&scl, 1)); k_busy_wait(100);
    if (levels[1] != 1) { return -EIO; }
    stage = 3;
    TRY(gpio_pin_set_dt(&sda, 0)); k_busy_wait(100);
    levels[2] = line_levels();
    TRY(gpio_pin_set_dt(&sda, 1)); k_busy_wait(100);
    levels[3] = line_levels();
    if (levels[2] != 2 || levels[3] != 3) { return -EIO; }

    stage = 4; TRY(gpio_pin_set_dt(&rst, 0));
    reset_released_ms = k_uptime_get();
    k_msleep(10);
    int64_t ready_until = k_uptime_get() + 2000;
    do {
        int result = monitor_step(-ETIMEDOUT);
        if (stage != 4) { return result; }
        k_msleep(1);
    } while (k_uptime_get() < ready_until);
    return -ETIMEDOUT;
}

static void probe_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (int i = 0; i < 4; i++) { levels[i] = -1; }
    int result = run_test();
    release_lines(result);
    int64_t next_report = 0;
    for (;;) {
        if (stage == 4 && result == -ETIMEDOUT) {
            result = monitor_step(result);
            if (stage != 4) { release_lines(result); }
        }
        if (k_uptime_get() >= next_report) {
            int live_rdy = gpio_is_ready_dt(&rdy) ? gpio_pin_get_dt(&rdy) : -ENODEV;
            LOG_INF("GPIO_ID v3 monitor result=%d stage=%u levels=%d,%d,%d,%d expected=3,1,2,3",
                    result, stage, levels[0], levels[1], levels[2], levels[3]);
            LOG_INF("GPIO_ID rdy_reset=%d rdy_ready=%d ack_count=%u ack_mask=0x%x product_valid=%d product=0x%04x",
                    rdy_reset, rdy_ready, ack_count, ack_mask, product_valid, product);
            LOG_INF("RDY_MON live=%d waiting=%d first_high_ms=%lld since_reset_ms=%lld",
                    live_rdy, stage == 4 && result == -ETIMEDOUT,
                    (long long)first_ready_ms,
                    (long long)(k_uptime_get() - reset_released_ms));
            next_report = k_uptime_get() + 5000;
        }
        k_msleep(stage == 4 && result == -ETIMEDOUT ? 1 : 100);
    }
}

/* Independent thread: does not block the system workqueue or keyboard init. */
K_THREAD_DEFINE(gpio_id_thread, 1536, probe_thread, NULL, NULL, NULL, 5, 0, 5000);
