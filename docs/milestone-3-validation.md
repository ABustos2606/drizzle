<!-- SPDX-License-Identifier: MIT -->
# Milestone 3 validation

**Guitar test passed; the Pi measurements are to do.** Recorded 21 and 22 October 2026. Done: Peepo's side of plan section 9, pinned at `v0.2.0` and checked under WSL; Drizzle built, tested and installed on the Pi and offered in its catalogue; the user's guitar test (passed, no default changed). To do: CPU and xruns on the Pi, on a sound power supply.

## Drizzle, release `v0.2.0`

Nothing in Drizzle needed to change for Peepo: milestone 2 (`8047478`, tagged `v0.2.0` and pushed) already declares `lv2:DelayPlugin` and `lv2:GeneratorPlugin` (not an instrument, which Peepo would withhold), `lv2:minorVersion 2` and `microVersion 0`, and installs `LICENSE` and `THIRD_PARTY_NOTICES` in the bundle. `ttl_check` holds the classes.

| Check | Result |
| --- | --- |
| Peepo's build step by hand under WSL (Debian 13, GCC 14.2): clone from GitHub, check out `8047478`, `make -j8 OPT=-O2 install` into a staging `LV2DIR` | Builds with no warnings; `drizzle.so`, `manifest.ttl`, `drizzle.ttl`, `drizzle_synth.ttl`, `LICENSE`, `THIRD_PARTY_NOTICES`; `ldd` clean |
| `make -j8 test` in that clone | PASS, every test; `match_beads` and `match_synth` bit for bit |

## Peepo

Edited in `C:\Pedal PI`, not committed:

| File | Change |
| --- | --- |
| `tools/ingen-spike/14-more-plugins.sh` | `third drizzle ... 8047478... "make" make -j"$JOBS" OPT="$DISCIPLINE_OPT" LV2DIR="$LV2DIR" install`, after Chamber (`-O2`, `-mcpu=cortex-a76` on the Pi) |
| `config/catalogue/inventory.txt`, `tools/catalogue-inventory.py` | `#drizzle` and `#synth` among Peepo's own plugins. `inventory.txt` edited by hand, as for Chamber (regenerating it would drop midi-cv and midi-pc) |
| `config/catalogue/overlay.json` | Drizzle: category Texture, a description, `"output": "output"`, `"bypass": "port"`, seven pages. Drizzle Synth: Sound source, a description, `"output": "output"`, six pages |
| `THIRD_PARTY_NOTICES.md` | A row for Drizzle and its vendored notice |
| `docs/38-eurorack-ports-roadmap.md` | Drizzle's checklist: what is done, what waits for the Pi |

Checks under WSL:

| Check | Result |
| --- | --- |
| `peepo_catalogue_export --only` the two URIs, built from Peepo's current tree, with the staged bundle on `LV2_PATH` | 2 exported, 2 available, 0 withheld. Drizzle: 27 controls, `enabled` taken as its bypass (not a page control), `latency` not shown. Drizzle Synth: 24 controls. Quality, CV assign to and Gate out come out as named choices; the "uses CV" switches, Freeze, Seed, Seed lock, Mono and Auto gain as switches; Pitch whole semitones; Input gain and Output in dB |
| `peepo_catalogue_tests` on the committed catalogue plus the two exported entries, with the new overlay | PASS (the overlay is refused if a page names a control a module does not show, or a control is in two pages; every offered module needs a written ASCII description) |
| `peepo_catalogue_tests` on Peepo's `config/catalogue` as edited | PASS |
| Every shown control in a page | Yes, both plugins |

The pages, as Peepo shows them:

| Drizzle page | Controls |
| --- | --- |
| Grain | Time, Size, Shape, Pitch |
| Density and mix | Density, Feedback, Dry/wet, Reverb |
| Play | Freeze, Seed, Seed lock, Quality |
| Modulation | Time mod, Size mod, Shape mod, Pitch mod |
| CV use | Time, Size, Shape and Pitch uses CV, Seed uses gate |
| Input and output | Auto gain, Input gain, Mono, Output |
| CV assign and out | CV assign to, Gate out |

| Drizzle Synth page | Controls |
| --- | --- |
| Grain | Time, Size, Shape, Pitch |
| Voice and mix | Density, Wavetable, Osc/grains, Reverb |
| Play | Freeze, Seed, Seed lock, Quality |
| Modulation | the four mod knobs |
| CV use | the four "uses CV" switches, Seed uses gate |
| CV assign and out | CV assign to, Gate out, Output |

Not done on the PC: `config/catalogue/wsl-debian.json` was not re-exported, for Chamber's reason (this WSL environment no longer matches it). Run `15-compat.sh` after a full `14-more-plugins.sh`.

## On the Pi, 22 October 2026

Drizzle `v0.2.0` (`8047478`) on the Pi 5 (`patchbox`, Debian 12), built as Peepo's build step does: cloned from GitHub into `/opt/peepo/src/more-plugins/drizzle`, `make -j3 OPT="-O2 -mcpu=cortex-a76"` (7 s, no warnings), installed into `/opt/peepo/lib/lv2`.

| Check | Result |
| --- | --- |
| `make test` on the Pi | PASS, every test, natively on arm64; `match_beads` and `match_synth` bit for bit |
| `ldd` | clean |
| Catalogue | The Pi's overlay and inventory differed from Peepo's working tree only by Drizzle's lines; copied over and re-exported with `tools/pi/export-catalogue.sh`: 336 -> 338 modules, Drizzle (27 controls) and Drizzle Synth (24) added and offered, nothing else changed. Backups in `/opt/peepo/share/peepo/pre-drizzle-20261022/` |
| Module list | The export copied to Peepo's `docs/evidence/catalogue-pi-20261022.json`; `docs/33-module-list.md` regenerated from it with `tools/document-catalogue.py` (now its default): 358 identities, 338 installed, Drizzle under Texture and Drizzle Synth under Sound source, both Ready |
| Engine, routing and interface restarted (JACK kept running) | All active; 554 plugins loaded, no errors from Drizzle; the startup patch reloaded clean (`dirty=0`); `peepo_effect_live show`: PASS |
| Power | `get_throttled` 0x50005 before the restart (under-voltage now and throttled), 0x50000 after: the power supply Chamber's record found is still in use |

## Listening

**The user's guitar test passed** (22 October 2026: "gorgeous"), with Mono on, Auto gain learning over the first seconds of playing, and starting points given for three sounds: a shimmer cloud (Density 0.82, Pitch +12, Time mod -0.3, Reverb 0.6), a frozen pad (Freeze on a ringing chord, Density 0.9) and a tape delay (Size 1.0, a tape quality, Feedback 0.5). No default was reported wrong, so none is changed: `v0.2.0` stays Peepo's pin.

## To do

1. `make test-timing` on the Pi, then `tools/pi/measure-effect.py` alone and in a typical patch (input -> amp -> Drizzle -> output, plus a Mist and a Drizzle Synth) for 30 minutes: Density at maximum, each quality, Mono on for one run. JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`, on a sound power supply, against the same patch without Drizzle.
2. Still open for later listening, each a **tune** in the spec: whether Mono should start on for a guitar (a mono source cabled to In L only records silence on the right); Density's default 0.7 (about two grains a second); Gate out's 10 ms trigger into Peepo's CV modules; Assign "All"'s fixed amounts; Drizzle Synth's defaults. Any change becomes `v0.2.1` (Turtle, C++ fallbacks and test host kept together by `ttl_check`) and Peepo's pin moves to it.
