/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* Use the nice!nano v2 board's own power-control pin and polarity.
 * The generic EXT_POWER device is disabled in the diagnostic shield, so
 * saved ext_power/state values cannot turn this rail off after startup.
 */
static const struct gpio_dt_spec vcc_control =
    GPIO_DT_SPEC_GET(DT_PATH(ext_power), control_gpios);

#define POWER_TEST_INIT_PRIORITY 81
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_EXT_POWER), "Generic power control must be off");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW), "RGB must be off");
BUILD_ASSERT(POWER_TEST_INIT_PRIORITY < CONFIG_DISPLAY_INIT_PRIORITY,
             "Power must settle before the OLED initializes");
BUILD_ASSERT(POWER_TEST_INIT_PRIORITY < CONFIG_INPUT_INIT_PRIORITY,
             "Power must settle before the TPS65 initializes");

static int corne_power_test_init(void) {
    if (!gpio_is_ready_dt(&vcc_control)) {
        return -ENODEV;
    }

    /* Hold the Corne RGB data line D1/P0.06 low. LEDs remain physically
     * powered, but receive no animation/data; power-cycle after flashing
     * to clear any colors latched by the previous firmware.
     */
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    int err = gpio_pin_configure(gpio0, 6, GPIO_OUTPUT_LOW);
    if (err) {
        return err;
    }
    err = gpio_pin_configure_dt(&vcc_control, GPIO_OUTPUT_ACTIVE);
    if (err) {
        return err;
    }
    k_msleep(500);
    return 0;
}

SYS_INIT(corne_power_test_init, POST_KERNEL, POWER_TEST_INIT_PRIORITY);
