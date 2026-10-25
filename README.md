# TAMISYNTH

A Tamagotchi-style pet for the M-VAVE FM-1 that lives on the drum beats you make, and teaches you to make them.

Every bar your pattern plays, the pet eats it. Each stage has a lesson, and bars that meet it make the pet happy and
grow it up: egg (a steady pulse), Blip (four on the floor), Kik (the backbeat), Hatling (syncopation and swing).
What it ate as a teen decides its adult form: Crate (boom bap), Fourfloor (house), Chopper (breaks) or Echo (dub).
It lives on powered-on time, since the FM-1 has no clock that runs while it is off: switched off, it sleeps.

## Controls

| | |
| --- | --- |
| keys F3..G#4 | the 16 steps of the selected lane |
| keys A4, A#4, B4 | kick, snare, hat, played live |
| OCT- / OCT+, KNOB1 | the lane: kick, snare, hat |
| KNOB2 / KNOB3 | tempo 60..180 / swing 0..50 % |
| PLAY | start / stop (stopping saves) |
| SAVE | save now |
| SEL + HOME, then HOME | a new egg |

## Install

Download `dinghy-X.Y.fwsc` from the latest release, plug the FM-1 in with a data cable, and run
`python3 tools/fm1_install.py dinghy-X.Y.fwsc` from this repository (`pip3 install Pillow mido python-rtmidi` once).
Keep M-VAVE's stock V15 at hand; holding OCT- at power-on is the USB rescue.

## Building

Sprites: `node tools/sprites.mjs` regenerates `firmware/src/sprites.h`. On a computer: `tests/run_host.sh
tests/scripts/pet_life.txt` runs a whole life on fast-forward (screens as PNG, sound as WAV). The firmware package
is built by GitHub Actions on every push to `main`.

Built on [Dinghy](https://github.com/zvenson/dinghy) (zvenson), on Felucca's platform (Leo Kuroshita, Hügelton
Instruments) as SLOOP (isod89) carries it. GPL-3.0-only; see LICENSING.md and docs/DINGHY-README.md.
