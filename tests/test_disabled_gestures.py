"""Run the pinned driver's actual handler against synthetic sensor frames."""
from pathlib import Path
import subprocess
import tempfile
from urllib.request import urlopen

REV = "e0f5e66e61e9daf961122f2f0a81be6b51b0ac9c"
BASE = f"https://raw.githubusercontent.com/deSales327/zmk-driver-azoteq-iqs5xx/{REV}/drivers/input/"
source = urlopen(BASE + "iqs5xx.c", timeout=30).read().decode()
header = urlopen(BASE + "iqs5xx.h", timeout=30).read().decode()
header = "\n".join(line for line in header.splitlines() if not line.startswith("#include"))
body = source[source.index("static void iqs5xx_work_handler("):
              source.index("static void iqs5xx_rdy_handler(")]
stubs = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define BIT(n) (1U<<(n))
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define K_FOREVER 0
#define K_MSEC(ms) (ms)
#define INPUT_BTN_0 0x100
#define INPUT_BTN_1 0x101
#define INPUT_REL_X 0
#define INPUT_REL_Y 1
#define INPUT_REL_HWHEEL 6
#define INPUT_REL_WHEEL 8
struct device { const void *config; void *data; };
struct k_work { int unused; };
struct k_work_delayable { int unused; };
struct i2c_dt_spec { int unused; };
struct gpio_dt_spec { int unused; };
struct gpio_callback { int unused; };
static int keys, moves, wheels, reads, fail_at, ends, scheduled;
static unsigned frame0, frame1;
static int movement, reset_seen;
static int input_report_key(const struct device *d, int code, int value, bool sync, int wait) {
    keys++; return 0;
}
static int input_report_rel(const struct device *d, int code, int value, bool sync, int wait) {
    if (code==INPUT_REL_X || code==INPUT_REL_Y) moves++; else wheels++;
    return 0;
}
static int k_work_cancel_delayable(struct k_work_delayable *w) { return 0; }
static int k_work_schedule(struct k_work_delayable *w, int delay) { scheduled++; return 0; }
"""
bus = r"""
static int iqs5xx_read_reg8(const struct device *d, uint16_t reg, uint8_t *value) {
    if (reads++==fail_at) return -5;
    *value=0;
    if (reg==IQS5XX_SYSTEM_INFO_0 && reset_seen) *value=IQS5XX_SHOW_RESET;
    if (reg==IQS5XX_SYSTEM_INFO_1 && movement) *value=IQS5XX_TP_MOVEMENT;
    if (reg==IQS5XX_GESTURE_EVENTS_0) *value=frame0;
    if (reg==IQS5XX_GESTURE_EVENTS_1) *value=frame1;
    if (reg==IQS5XX_NUM_FINGERS) *value=1;
    return 0;
}
static int iqs5xx_read_reg16(const struct device *d, uint16_t reg, uint16_t *value) {
    if (reads++==fail_at) return -5;
    *value=reg==IQS5XX_REL_X ? 5 : (uint16_t)-7;
    return 0;
}
static int iqs5xx_write_reg8(const struct device *d, uint16_t reg, uint8_t value) { return 0; }
static int iqs5xx_end_comm_window(const struct device *d) { ends++; return 0; }
"""
cases = r"""
static struct iqs5xx_config config;
static struct iqs5xx_data data;
static struct device dev;
static void clear(void) {
    memset(&config,0,sizeof(config)); memset(&data,0,sizeof(data));
    dev.config=&config; dev.data=&data; data.dev=&dev;
    keys=moves=wheels=reads=ends=scheduled=reset_seen=0; fail_at=-1;
    movement=1; frame0=frame1=0;
}
int main(void) {
    /* Even all stale/default gesture flag combinations cannot press a button. */
    for (unsigned a=0;a<256;a++) for (unsigned b=0;b<256;b++) {
        for (int moving=0;moving<2;moving++) {
            clear(); frame0=a; frame1=b; movement=moving;
            iqs5xx_work_handler(&data.work);
            assert(keys==0 && scheduled==0 && wheels==0 && !data.active_hold);
            assert(data.buttons_pressed==0 && moves==2*moving && ends==1);
        }
    }
    for (int i=0;i<7;i++) {
        clear(); fail_at=i; frame0=frame1=0xff;
        iqs5xx_work_handler(&data.work);
        assert(keys==0 && scheduled==0 && moves==0 && ends==1);
    }
    clear(); reset_seen=1; frame0=frame1=0xff;
    iqs5xx_work_handler(&data.work); assert(keys==0 && moves==0 && ends==1);
    /* Enabled configurations still support their intended gestures. */
    clear(); config.one_finger_tap=true; frame0=IQS5XX_SINGLE_TAP;
    iqs5xx_work_handler(&data.work); assert(keys==1 && scheduled==1 && moves==0);
    clear(); config.two_finger_tap=true; frame1=IQS5XX_TWO_FINGER_TAP;
    iqs5xx_work_handler(&data.work); assert(keys==1 && scheduled==1 && moves==0);
    clear(); config.press_and_hold=true; frame0=IQS5XX_PRESS_AND_HOLD;
    iqs5xx_work_handler(&data.work); assert(keys==1 && data.active_hold);
    frame0=0; iqs5xx_work_handler(&data.work); assert(keys==2 && !data.active_hold);
    clear(); config.scroll=true; frame1=IQS5XX_SCROLL;
    iqs5xx_work_handler(&data.work); assert(keys==0 && moves==0 && data.scroll_x_acc==5);
    puts("PASS: 131072 disabled-gesture frames, 7 read failures, reset and 4 enabled-gesture cases");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / "handler.c"
    exe = Path(tmp) / "handler-test"
    c_file.write_text(stubs + header + bus + body + cases)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Werror", str(c_file), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
