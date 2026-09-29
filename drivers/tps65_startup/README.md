# Source and scope

Derived from AYM1607/zmk-driver-azoteq-iqs5xx at
27321f0232b50f0af31eb27ff97d539933467ea4 (MIT, Mariano Uvalle).

iqs5xx.h is unchanged. iqs5xx.c changes only initialization and the RDY
callback guard. The runtime movement/gesture processing and setup register
values remain identical to that revision. This experiment requires NRST.

Startup timing: reset held active during 1000 ms settling; each attempt holds
reset for 10 ms, releases it, waits 10 ms, then samples RDY every 1 ms for up
to 2000 ms. Setup begins as soon as RDY is high. Three attempts maximum,
separated by 250 ms on failure; the existing I2C transfer timeout still applies.
Callback installation and IRQ enabling happen only after successful setup.

Protocol reference: Azoteq IQS5xx-B000 datasheet revision 2.1, sections 8.1,
8.6 and 8.7. RDY is high during a communication window; delaying further
after it goes high could miss that window. Fixed delays here are experimental
margins, not claimed manufacturer requirements.
