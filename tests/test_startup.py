"""Run the actual driver startup functions against a virtual GPIO/I2C clock.

This tests control flow, not electrical behavior or the Zephyr implementation.
Requires a C compiler (the Linux CI runner supplies gcc).
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).parents[1] / "drivers/tps65_startup/iqs5xx.c").read_text()
start = source.index("static int iqs5xx_wait_ready(")
end = source.index("// Replace CONFIG_INPUT_INIT_PRIORITY", start)
startup = source[start:end]

stubs = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define GPIO_INPUT 1
#define GPIO_OUTPUT_ACTIVE 2
#define GPIO_INT_DISABLE 0
#define GPIO_INT_EDGE_RISING 3
#define BIT(n) (1U << (n))
struct gpio_dt_spec { void *port; int pin; };
struct i2c_dt_spec { int addr; };
struct iqs5xx_config {
    struct gpio_dt_spec rdy_gpio, reset_gpio;
    struct i2c_dt_spec i2c;
};
struct iqs5xx_data {
    const void *dev;
    bool initialized;
    int work, button_release_work, rdy_cb;
};
struct device { const struct iqs5xx_config *config; struct iqs5xx_data *data; };
static int64_t now, released_at;
static int ready_after, failures, attempts, setups, submits, irq_error, read_error;
static bool irq, reset, callback, pending_window;
static struct iqs5xx_data data;
static struct iqs5xx_config config = {
    .rdy_gpio = {(void*)1, 6}, .reset_gpio = {(void*)1, 4}, .i2c = {0x74}
};
static struct device dev = {&config, &data};
static int64_t k_uptime_get(void) { return now; }
static void k_msleep(int ms) { now += ms; }
static bool i2c_is_ready_dt(const void *p) { return true; }
static bool gpio_is_ready_dt(const void *p) { return true; }
static void iqs5xx_work_handler(void) {}
static void iqs5xx_button_release_work_handler(void) {}
static void iqs5xx_rdy_handler(void) {}
static void k_work_init(void *p, void (*f)(void)) {}
static void k_work_init_delayable(void *p, void (*f)(void)) {}
static void gpio_init_callback(void *p, void (*f)(void), unsigned pins) {}
static int gpio_add_callback(void *port, void *cb) { callback = true; return 0; }
static int gpio_remove_callback(void *port, void *cb) { callback = false; return 0; }
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p, int flags) {
    if (p == &config.reset_gpio) reset = true;
    return 0;
}
static int gpio_pin_set_dt(const struct gpio_dt_spec *p, int active) {
    assert(!irq && !data.initialized);
    reset = active;
    if (!active) { released_at = now; attempts++; }
    return 0;
}
static int gpio_pin_get_dt(const struct gpio_dt_spec *p) {
    if (read_error) return -EIO;
    if (data.initialized) return pending_window;
    return !reset && ready_after >= 0 && now - released_at >= ready_after;
}
static int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *p, int mode) {
    if (mode == GPIO_INT_EDGE_RISING) {
        assert(data.initialized && callback && setups > failures);
        if (irq_error) return -EIO;
        irq = true;
    } else irq = false;
    return 0;
}
static int iqs5xx_setup_device(const struct device *d) {
    assert(!irq && !data.initialized && !reset && now >= 1020);
    assert(gpio_pin_get_dt(&config.rdy_gpio) == 1);
    setups++;
    return setups <= failures ? -EIO : 0;
}
static void k_work_submit(void *p) {
    assert(data.initialized && irq);
    submits++;
}
"""

cases = r"""
static void clear(void) {
    now = released_at = 0;
    ready_after = 180;
    failures = attempts = setups = submits = irq_error = read_error = 0;
    irq = reset = callback = pending_window = false;
    data = (struct iqs5xx_data){0};
}
int main(void) {
    clear();
    assert(iqs5xx_init(&dev) == 0);
    assert(now == 1190 && setups == 1 && attempts == 1 && irq);

    clear(); ready_after = 1500;
    assert(iqs5xx_init(&dev) == 0);
    assert(now == 2510 && setups == 1);

    clear(); ready_after = -1;
    assert(iqs5xx_init(&dev) == -ETIMEDOUT);
    assert(attempts == 3 && setups == 0 && now == 7560);
    assert(!irq && !callback && !data.initialized);

    clear(); failures = 2;
    assert(iqs5xx_init(&dev) == 0);
    assert(attempts == 3 && setups == 3 && irq);

    clear(); failures = 3;
    assert(iqs5xx_init(&dev) == -EIO);
    assert(attempts == 3 && setups == 3 && !irq && !data.initialized);

    clear(); read_error = 1;
    assert(iqs5xx_init(&dev) == -EIO);
    assert(setups == 0 && attempts == 3 && !irq);

    clear(); irq_error = 1;
    assert(iqs5xx_init(&dev) == -EIO);
    assert(!irq && !callback && !data.initialized);

    clear(); pending_window = true;
    assert(iqs5xx_init(&dev) == 0);
    assert(submits == 1);
    puts("PASS: 8 startup scenarios using actual driver functions");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / "startup.c"
    binary = Path(tmp) / "startup-test"
    c_file.write_text(stubs + startup + cases)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Werror", str(c_file), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
