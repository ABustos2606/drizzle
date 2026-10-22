<!-- SPDX-License-Identifier: MIT -->
# Milestone 3 validation

**In progress.** Recorded 21 October 2026. Done: Peepo's side of plan section 9 on the PC, pinned at `v0.2.0`, and checked with Peepo's catalogue exporter and tests under WSL. To do: everything on the Pi (installation, CPU and xruns, the user's guitar test) and the tuning it leads to, in `v0.2.x` tags.

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

## On the Pi (to do)

1. Peepo's build step for Drizzle at the pinned commit; `make test` there (natively on arm64).
2. `make test-timing` there, then `tools/pi/measure-effect.py` alone and in a typical patch (input -> amp -> Drizzle -> output, plus a Mist and a Drizzle Synth) for 30 minutes with guitar or a looped DI: Density at maximum, each quality, Mono on for one run. JACK xruns, DSP load and RSS into Peepo's `docs/evidence/`. Chamber's record found the Pi's power supply causing xruns (under-voltage): measure on a sound supply, and compare against the same patch without Drizzle.
3. Re-export the Pi's catalogue (`tools/pi/export-catalogue.sh`); `docs/33-module-list.md` regenerated with `tools/document-catalogue.py`.
4. Listening with guitar. What to judge, each a **tune** in the spec:
    - **Quality**'s default (Digital, the hardware's): does a tape quality suit a guitar better as the first sound?
    - **Density**'s default 0.7, with Seed lock on: grains busy enough to hear at once, not a wash.
    - **Auto gain** on guitar: it learns over the first seconds of sound and aims at a peak of 0.9. Too hot into the soft clipping, or right? Otherwise a manual Input gain as the default.
    - **Dry/wet** 0.5, **Feedback** 0, **Reverb** 0: the first impression of a new Drizzle.
    - **Gate out**'s 10 ms trigger into Peepo's envelopes and sequencers.
    - **Assign "All"**'s fixed amounts: whether they need ports.
    - Drizzle Synth: its defaults as a sound source.

Then `v0.2.1` with the new defaults (Turtle, the C++ fallbacks and the test host kept together by `ttl_check`), and Peepo's pin moved to it.
