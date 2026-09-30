"""Exercise the actual diagnostic functions with GPIO and time simulated."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).parents[1] / "src/tps65_gpio_identify.c").read_text()
body = source[source.index("static int64_t transfer_deadline;"):
              source.index("static void probe_thread(")]
stubs = r"""
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#define GPIO_INPUT 1
#define GPIO_OUTPUT_HIGH 2
#define GPIO_OUTPUT_ACTIVE 4
struct gpio_dt_spec { int pin; };
static const struct gpio_dt_spec sda={0}, scl={1}, rst={2}, rdy={3};
static int state[4], scenario, id_bit, stretch_reads;
static int64_t now_us;
static int simulated_get(const struct gpio_dt_spec *p);
static int gpio_pin_get_dt(const struct gpio_dt_spec *p) { return simulated_get(p); }
static int gpio_pin_set_dt(const struct gpio_dt_spec *p, int v) { state[p->pin]=v; return 0; }
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p, int flags) {
    if (p!=&rdy) state[p->pin]=1;
    return 0;
}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *p) { return true; }
static int64_t k_uptime_get(void) { return now_us/1000; }
static void k_usleep(int us) { now_us+=us; }
static void k_busy_wait(int us) { now_us+=us; }
static void k_msleep(int ms) { now_us+=(int64_t)ms*1000; }
"""
cases = r"""
static int simulated_get(const struct gpio_dt_spec *p) {
    if (p==&rdy) {
        if (scenario==2) return 0;
        if (scenario==6) return !state[rst.pin] && now_us>=6000000;
        if (scenario==7 && now_us>=6000000) return -EIO;
        if (scenario==7) return 0;
        return !state[rst.pin];
    }
    if (p==&scl) {
        if (scenario==1) return 1; /* host cannot pull clock low */
        if (stage>=10 && scenario==3) return 0; /* clock held low */
        if (stage>=10 && scenario==5 && stretch_reads++<40) return 0;
        return state[scl.pin];
    }
    if (p==&sda && stage>=10) {
        if (stage==10 || stage==14 || stage==19) return state[sda.pin];
        if (stage==16 || stage==17) {
            return (0x0226 >> (15-id_bit++)) & 1;
        }
        if (stage==11 || stage==12 || stage==13 || stage==15 ||
            stage==20 || stage==21 || stage==22 || stage==23) {
            return scenario==4 && stage==11 ? 1 : 0;
        }
    }
    return state[p->pin];
}
static void clear(int mode) {
    scenario=mode; now_us=0; id_bit=stretch_reads=0;
    stage=ack_count=ack_mask=product=0; product_valid=false;
    reset_released_ms=0; first_ready_ms=-1;
    for (int i=0;i<4;i++) { state[i]=0; levels[i]=-1; }
}
int main(void) {
    clear(0); assert(run_test()==0);
    assert(stage==25 && product_valid && product==0x0226);
    assert(ack_count==8 && ack_mask==0xff);
    assert(levels[0]==3 && levels[1]==1 && levels[2]==2 && levels[3]==3);
    clear(1); assert(run_test()==-EIO && stage==2 && ack_count==0);
    clear(2); assert(run_test()==-ETIMEDOUT && stage==4 && ack_count==0);
    assert(now_us<2200000);
    clear(3); assert(run_test()==-ETIMEDOUT && stage==10 && ack_count==0);
    assert(now_us<700000);
    clear(4); assert(run_test()==-ENXIO && stage==11);
    assert(ack_count==1 && ack_mask==0 && !product_valid);
    clear(5); assert(run_test()==0 && product_valid && stretch_reads>=40);
    clear(6); int result=run_test();
    assert(result==-ETIMEDOUT && stage==4 && ack_count==0);
    release_lines(result);
    while (stage==4) { k_msleep(1); result=monitor_step(result); }
    assert(result==0 && product_valid && first_ready_ms>=5800);
    unsigned completed_acks=ack_count;
    assert(monitor_step(result)==0 && ack_count==completed_acks);
    release_lines(result);
    assert(state[sda.pin]==1 && state[scl.pin]==1 && state[rst.pin]==0);
    clear(2); result=run_test(); release_lines(result);
    for (int i=0;i<30000;i++) { k_msleep(1); result=monitor_step(result); }
    assert(result==-ETIMEDOUT && stage==4 && ack_count==0 && first_ready_ms==-1);
    clear(7); result=run_test(); release_lines(result); now_us=6000000;
    assert(monitor_step(result)==-EIO && stage==5 && ack_count==0);
    clear(4); result=run_test(); release_lines(result);
    assert(monitor_step(result)==-ENXIO && ack_count==1);
    clear(1); result=run_test(); release_lines(result);
    assert(monitor_step(result)==-EIO && stage==2 && ack_count==0);
    puts("PASS: 11 GPIO/identity scenarios including late RDY, permanent low, read error and no retries");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c_file=Path(tmp)/"gpio.c"
    exe=Path(tmp)/"gpio-test"
    c_file.write_text(stubs+body+cases)
    subprocess.run(["gcc","-std=c11","-Wall","-Werror",str(c_file),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
