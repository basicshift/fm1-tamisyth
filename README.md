# TAMISYNTH

A Tamagotchi-style pet for the M-VAVE FM-1 that lives on the drum beats you make, and teaches you to make them.

Pick a library (drums, percussion, bass or lead) and you get one of its three eggs at random. Raise it by
learning that library: every bar your pattern plays, the pet eats it, and bars that meet its lesson grow it from
egg to baby, child, teen and adult. What it ate as a teen decides its adult form. Raised pets live in the nest,
and the band puts one pet per library together to play a song.

The screen always says what to do next, and the keys and buttons to press blink: tap the blinking steps, press
the blinking note (bass), pick the row with OCT, press PLAY. The baby and child stages learn a training song:

| library | baby | child | teen |
| --- | --- | --- | --- |
| drums | Stomp stomp clap (boom boom clap) | Rock beat | your own, off the beat or swung |
| bass | Twinkle Twinkle | In the Hall of the Mountain King (Grieg) | your own, on the off-beats |

The songs are public-domain tunes or bare rhythms; a song stage passes when every note is there, with a couple of
extras allowed.

| library | eggs | adult forms | status |
| --- | --- | --- | --- |
| drums | Tomcat, Kitbash, Rimshot | boom bap, house, breaks, dub | 0.1 |
| percussion | (to come) | | 0.3 |
| bass | Deep Dragon, Ember Dragon, Frost Dragon | sub, acid, disco, funk | 0.2 |
| lead | (to come) | | 0.3 |

Art is 1-bit, ink on a dark LCD: each strip in `assets/art` is a PNG (white = ink) with a JSON naming its frames.
Edit them in any pixel editor and run `python3 tools/mono.py` to rebuild `firmware/src/art.h`. Deep Dragon and the
icons come from the sheet in `assets/source`; Tomcat comes from its sheet there too. The drum line (`node tools/sprites.mjs`) stands in for Kitbash and Rimshot, and the second and third
eggs of the other libraries are placeholders until their own art lands. The pets live on powered-on time, since the FM-1
has no clock that runs while it is off: switched off, they sleep.

## Controls

| | |
| --- | --- |
| HOME | the nest: your pets, a new egg, the band |
| nest and menus | OCT- / OCT+ or KNOB1 move, SEL opens |
| keys F3..G#4 | the 16 steps |
| PLAY / KNOB2 / KNOB3 / SAVE | start and stop (stopping saves) / tempo / swing / save now |
| SEQ / SEL + SEQ | clear the lane (bass: the notes) / clear the whole pattern |
| SEQ twice, in the nest | release the pet under the cursor |
| drums | OCT- / OCT+ or KNOB1 the lane (kick, snare, hat); keys A4, A#4, B4 play them live |
| bass | keys A4..G5 pick a note (held step keys take it); OCT- / OCT+ the octave; KNOB1 nudges the note |
| band | OCT- / OCT+ a part, KNOB1 its pet, KNOB2 tempo, PLAY |

## Install

Download `dinghy-X.Y.fwsc` from the latest release, plug the FM-1 in with a data cable, and run
`python3 tools/fm1_install.py dinghy-X.Y.fwsc` from this repository (`pip3 install Pillow mido python-rtmidi` once).
Keep M-VAVE's stock V15 at hand; holding OCT- at power-on is the USB rescue.

## Building

Sprites: `node tools/sprites.mjs` regenerates `firmware/src/sprites.h`. On a computer: `tests/run_host.sh
tests/scripts/drums_life.txt` (or `bass_life.txt`) runs a whole life on fast-forward (screens as PNG, sound as WAV). The firmware package
is built by GitHub Actions on every push to `main`.

Built on [Dinghy](https://github.com/zvenson/dinghy) (zvenson), on Felucca's platform (Leo Kuroshita, Hügelton
Instruments) as SLOOP (isod89) carries it. GPL-3.0-only; see LICENSING.md and docs/DINGHY-README.md.
