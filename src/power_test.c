/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <drivers/ext_power.h>

#define POWER_NODE DT_PATH(ext_power)
#define POWER_SETTING "ext_power/state/" DEVICE_DT_NAME(POWER_NODE)

BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_EXT_POWER), "Use the standard power driver");
BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS), "Recovery needs settings support");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW), "RGB must stay off");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_SLEEP), "Keep power on during this test");
BUILD_ASSERT(DT_NODE_HAS_STATUS(POWER_NODE, okay), "External power must be enabled");
BUILD_ASSERT(81 < CONFIG_DISPLAY_INIT_PRIORITY, "Power must settle before OLED init");
BUILD_ASSERT(81 < CONFIG_INPUT_INIT_PRIORITY, "Power must settle before TPS65 init");

/* Zephyr selects the most specific settings handler. Intercept only this
 * device's saved state, BEFORE the generic parent handler can apply an old
 * 'off' value. Re-enabling after an off pulse could leave an already initialized
 * OLED or TPS65 without its configuration. All other settings load normally.
 */
static int recover_power_setting(const char *name, size_t len,
                                 settings_read_cb read_cb, void *cb_arg) {
    if (name && *name) {
        return -ENOENT;
    }
    if (len == 0) {
        return 0;
    }
    if (len != sizeof(bool)) {
        return -EINVAL;
    }
    bool previous_state;
    ssize_t bytes = read_cb(cb_arg, &previous_state, sizeof(previous_state));
    if (bytes < 0) {
        return bytes;
    }
    /* Deliberately do not apply previous_state, whether true or false. */
    return bytes == sizeof(previous_state) ? 0 : -EINVAL;
}

static int commit_power_recovery(void) {
    const struct device *power = DEVICE_DT_GET(POWER_NODE);
    if (!device_is_ready(power)) {
        return -ENODEV;
    }
    int err = ext_power_enable(power);
    if (err) {
        return err;
    }
    /* Backend is initialized and loading has finished before h_commit.
     * Repair the original key as well, so rollback does not restore VCC-off.
     * Do not erase Bluetooth bonds, Studio edits, or RGB preferences.
     */
    const bool enabled = true;
    return settings_save_one(POWER_SETTING, &enabled, sizeof(enabled));
}

SETTINGS_STATIC_HANDLER_DEFINE(corne_power_recovery, POWER_SETTING, NULL,
                               recover_power_setting, commit_power_recovery, NULL);

static int corne_rgb_data_low(void) {
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio0)) {
        return -ENODEV;
    }
    /* VCC uses the stock nice!nano driver, including its inactive -> active
     * startup transition and delay. Only RGB data is managed here.
     */
    return gpio_pin_configure(gpio0, 6, GPIO_OUTPUT_LOW);
}

SYS_INIT(corne_rgb_data_low, POST_KERNEL, 80);
