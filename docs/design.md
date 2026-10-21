# Drizzle design

Drizzle is a granular processor and delay with a built-in reverb, for Peepo, a pedal built on a Raspberry Pi 5 running the Ingen modular host. It is an LV2 plugin made from the open-source code of Beads, a eurorack module by Emilie Gillet and the successor of Clouds. It must also work as a normal LV2 plugin in Carla or Ardour.

**Written:** 20 October 2026, from Peepo's `docs/38-eurorack-ports-roadmap.md` and a reading of Beads' source at `pichenettes/eurorack` commit `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026, the first public commit of Beads' code). **Status:** spec draft, nothing built.

## Why Beads

- Peepo already has Mist, Poly's port of Clouds with the Parasites firmware. Beads is Clouds rebuilt: one granular engine with no separate modes, better grain scheduling, a delay mode, tape and digital quality settings, and a reverb.
- Its source was published under the MIT license in October 2026, so a port can be shipped with Peepo.
- No LV2 port exists. Poly's `lenticular_lv2` predates it.
- Its `dsp/` code has no hardware-specific instructions (no CMSIS, no inline assembly) and already runs on a desktop in its own `test/beads_test.cc`.

## Where it lives

Drizzle has its own repository (https://github.com/ABustos2606/drizzle), outside Peepo:

- Peepo builds every plugin from another repository at a pinned commit. Drizzle is built the same way.
- It is a normal LV2 bundle, so it can be tested on a desktop without Ingen or the Pi.
- Its build is a plain Makefile.
- Its license is **MIT**, the same as Beads' code. Peepo (GPL-3.0-or-later) can build and ship it.
- Tagged releases let Peepo update to a new version on purpose.

What stays in Peepo:

- its entries in `config/catalogue/overlay.json` and `config/catalogue/inventory.txt`;
- the build step that fetches and builds it at a pinned commit;
- patches that use it, xrun measurements and test notes.

The plugin URIs are `https://github.com/ABustos2606/drizzle#...`, not Peepo's namespace. Fix them before the first saved patch, because changing them later breaks patches.

## Name

"Drizzle", in the same weather family as Mist (Peepo's Clouds). Mutable Instruments' guidelines ask derivative works not to use the trademark and not to keep the module's name (Peepo `docs/37-module-names.md`). "Beads" appears only in credits.

## What Beads does

From the source (`beads.cc`, `ui.cc`, `dsp/processor.h`):

- **One granular engine.** Grains are read from a recording buffer that always records the input unless frozen. Time (where in the buffer), Size (grain length), Shape (grain envelope) and Pitch set each grain; Density sets how many grains are triggered, with a free-running, clocked or gated behaviour depending on the Seed input and button. With Size above 0.99 the engine crossfades into a multitap delay (`delay_controller.cc`, `multitap_delay.h`): Time and Shape then set the taps, and Density and Seed clock them.
- **Quality ("media").** Four settings: dirty tape, clean tape, vintage digital and digital. They change the buffer's bit depth and length and the soft clipping on the input. **On the hardware the vintage digital setting also runs the whole module at 32 kHz** (`Processor::sample_rate()`), and the tape settings process at half rate inside (`ratio()`).
- **Attenurandomizers.** Time, Size, Shape and Pitch each have one bipolar amount knob, and what it does depends on whether a CV cable is in (`dsp/randomized_parameter.h`): with no cable, right of noon is a smooth random walk and left of noon is a new random value per grain; with a cable, right of noon scales the CV and left of noon adds random modulation to it.
- **Mix.** Feedback, Dry/wet and Reverb, plus an assignable CV input routed to one of them or all three.
- **Freeze** (button and gate input) stops recording. **Seed** (button and gate input) triggers grains; held or locked it changes the trigger behaviour.
- **Synth mode.** With no audio input for a while, Beads becomes a wavetable synthesizer (`dsp/synth.cc`), played by the same controls.
- **Monophonic mode** (Media + Seed) and **gate output mode** (Assign + Seed) are hidden settings.
- **Gain.** Automatic input gain, or a manual gain setting.

## Plugins

Two plugins, one bundle, one binary:

| Plugin | URI | What it is |
| --- | --- | --- |
| Drizzle | `https://github.com/ABustos2606/drizzle#drizzle` | The granular processor and delay. Stereo in, stereo out |
| Drizzle Synth | `https://github.com/ABustos2606/drizzle#synth` | Beads' synth mode as a sound source. No audio input |

A plugin can't tell whether its input is cabled, so Beads' automatic switch to synth mode becomes a second plugin rather than a guess (decided 20 October 2026).

## Controls

Draft. Symbols are fixed in milestone 0 and then never change.

| Control | Kind | Notes |
| --- | --- | --- |
| Time | 0-1 | Position in the buffer |
| Size | 0-1 | Grain size; delay at the top |
| Shape | 0-1 | Grain envelope |
| Pitch | semitones, -24 to +24 | Whole semitones as in Mist; fine pitch from CV |
| Density | 0-1 | Grain rate; noon is the hardware's zero point |
| Time mod, Size mod, Shape mod, Pitch mod | -1 to 1, 0 | The hardware's attenurandomizer, with the same meaning on each side of noon |
| Time uses CV, Size uses CV, Shape uses CV, Pitch uses CV | switch, off | Stands in for "a cable is in" (`has_cv`), which a plugin can't detect. Off: the mod knob randomises. On: it scales the CV input |
| Feedback, Dry/wet, Reverb | 0-1 | |
| Freeze | switch | Also a gate input |
| Seed | momentary | Also a gate input; triggers a grain |
| Seed lock | switch | The hardware's Seed + Freeze combo |
| Quality | Dirty tape, Clean tape, Vintage digital, Digital | `Processor::Configure` switches it while running |
| Mono | switch | The hardware's monophonic mode |
| Gain | Auto, or a fixed input gain | The hardware's gain setting |
| Output | dB | Peepo's output level control (`"output"` in the overlay) |

CV inputs: Time, Size, Shape, Pitch (V/oct, Poly's scale: 1.0 = 5 V, so 0.2 per octave), Density, Assign (with a destination control: Feedback, Dry/wet, Reverb or all), Freeze gate, Seed gate. CV output: the grain trigger or gate (the hardware's gate output mode becomes a choice control).

Drizzle Synth has the same controls minus Freeze, Feedback, Dry/wet, Gain and the audio input.

## Sample rate

Peepo runs at 48 kHz. Beads' digital and tape qualities run at 48 kHz on the hardware, so they run unchanged. Vintage digital runs at 32 kHz on the hardware. In that quality Drizzle resamples 48 -> 32 -> 48 kHz around the processor, so it sounds like the hardware (decided with the user on 20 October 2026). Switching into or out of vintage digital crossfades between the two paths. Check against `beads_test` renders at 32 kHz.

Drizzle must also run at other rates in desktop hosts: below or above 48 kHz it resamples to 48 kHz, or refuses to instantiate. **Decision for milestone 0.**

## Memory

About 0.8 MB per instance (768 KB delay buffers, 64 KB reverb), allocated in `instantiate`. The buffer is not saved with the patch. Saving it as LV2 state is possible later (about 1 MB per instance).

## Bypass

Beads has no bypass. If Peepo's engine bypass is used, a frozen buffer stops and tails cut. **Open question:** add an `lv2:enabled` port so tails ring out, with `"bypass": "port"` in Peepo's overlay, as Discipline does.

## Decisions

Settled with the user on 20 October 2026:

- **Drizzle Synth is a separate plugin** in the same bundle, not a switch on Drizzle.
- **Vintage digital runs at 32 kHz**, with resampling, as on the hardware.

## Open questions

| Question | Why it matters | When to settle |
| --- | --- | --- |
| Other host rates | Desktop hosts may run at 44.1 or 96 kHz | Milestone 0 |
| Bypass port | Tails and frozen buffers under engine bypass | Milestone 1 |
| Saving the buffer | Lost on reload, like Mist | After milestone 3 |
| CPU on the Pi | Grain count at high Density, the reverb | Milestone 1, measured |

## Milestones

- [ ] Milestone 0: vendored code, Makefile, LV2 glue and `.ttl` with the full port list. Drizzle passes audio through the processor at default settings. Loads in jalv and in Ingen under WSL.
- [ ] Milestone 1: every control and CV input, the four qualities, Freeze and Seed. Renders match `beads_test` at the same settings. Pi CPU and xrun check.
- [ ] Milestone 2: Drizzle Synth, Seed lock, Mono, Gain, the gate output, the Assign input.
- [ ] Milestone 3: Peepo integration: overlay entry, pinned build, listening with guitar on the Pi. Tag `v0.1.0`.
