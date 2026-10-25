# Clock probe

Does the M-VAVE FM-1 keep time while it is switched off? This firmware (built on Dinghy) finds out.

It reads the chip's power-management (P33) and RTC (R3) registers once a second, finds the ones that count up,
and saves them to flash every 5 s. At the next power-on it compares, and the bottom of the screen says:

- **R3 xx  1/s  off 612 s** (green): that counter kept running while the FM-1 was off. The FM-1 keeps time.
- **RESET** (yellow): the counter started over at power-on. No clock while off.
- **off 0 s**: the counter paused while off.
- **no steady counter found**: nothing on the bus counts; the FM-1 has no usable clock.

## The test

1. Install the `.fwsc` with `python3 tools/fm1_install.py dinghy.fwsc` (USB, asks before it writes). Keep the stock V15 at hand.
2. Let it run 30 s. Note the time on your phone. Switch the FM-1 off and unplug USB.
3. Wait 10 minutes. Switch it on. After 20 s, read the bottom lines and compare the "off" seconds to your phone.
4. Optional: repeat with the battery run flat, to see if the clock survives that.

HOME forgets the record. Nothing is written outside the firmware's own settings sector.
Back to anything else: the Hub, or hold OCT- at power-on for the USB rescue.
