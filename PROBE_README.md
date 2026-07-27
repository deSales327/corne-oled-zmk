# P0.06 PWM Probe

Diagnostic-only firmware. It disables the WS2812 SPI driver and drives P0.06 as a normal PWM output.

- Starts at 100% duty cycle.
- Raise + X toggles high/low.
- Raise + C forces high.
- Raise + V forces low.

Measure P0.06 relative to GND with a multimeter. Do not merge this branch into daily-use firmware.
