# Drizzle

An LV2 granular processor and delay with a built-in reverb, written for Peepo
and other LV2 hosts. MIT licensed, Copyright (c) 2026 Alejandro Bustos.

**Status, 21 October 2026: milestone 0 done, milestone 1 done offline.**
Drizzle plays: every control and CV input except Mono, gain, the assignable
CV and the gate output (milestone 2), the four qualities at the hardware's
rates, Freeze and Seed. It still needs its CPU and xrun check on the Pi.
Drizzle Synth loads but is silent until milestone 2. See
[docs/milestone-1-validation.md](docs/milestone-1-validation.md).

Drizzle records its input into a buffer and plays it back as grains: Time
picks where in the buffer, Size the grain length, Shape the grain envelope,
Pitch transposes, and Density sets how many grains play. At large Size it
becomes a delay. Feedback, Dry/wet and Reverb mix the result. Freeze holds the
buffer; Seed triggers grains. Four quality settings, from dirty tape to clean
digital, change the buffer and the colour of the sound. Drizzle Synth, a
second plugin in the same bundle, plays the same engine as a wavetable synth.

| Plugin | URI |
| --- | --- |
| Drizzle | `https://github.com/ABustos2606/drizzle#drizzle` |
| Drizzle Synth | `https://github.com/ABustos2606/drizzle#synth` |

Build on Linux with a C++17 compiler, GNU Make, pkg-config and LV2 headers
(`build-essential pkg-config lv2-dev` on Debian). The build needs no network:

```sh
make
make install PREFIX=/usr/local
make install LV2DIR=/opt/peepo/lib/lv2     # where Peepo finds plugins
make OPT="-O2 -mcpu=cortex-a76"            # native Pi build
make test
make test-timing                           # 10 simulated minutes of random controls
make test-hosts                            # jalv and Ingen (Peepo's env.sh first)
make render && build/render build/drizzle.lv2/drizzle.so out.wav --in guitar.wav dry_wet=0.7
```

`make test` needs `python3-rdflib`; `make test-hosts` needs jackd, jalv,
Ingen, `python3-jack-client` and numpy. `tools/vendor.sh` refreshes the
vendored code (it needs network access; the build never runs it).

The behaviour specification is [docs/design.md](docs/design.md); the build
and milestone requirements are in
[docs/implementation-plan.md](docs/implementation-plan.md). Do not rename
plugin URIs or port symbols after patches use them. Peepo's catalogue overlay,
pinned build integration and hardware evidence belong in Peepo's repository.

## Credits

Drizzle's sound engine is the code of Beads, a eurorack module designed by
Emilie Gillet, published under the MIT license at
https://github.com/pichenettes/eurorack. The vendored files in
`third_party/eurorack/` keep their copyright notices. Drizzle is not
affiliated with or endorsed by Mutable Instruments, a registered trademark.
