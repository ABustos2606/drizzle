# Drizzle

An LV2 granular processor and delay with a built-in reverb, written for Peepo
and other LV2 hosts. MIT licensed, Copyright (c) 2026 Alejandro Bustos.

**Status, 20 October 2026: planned.** This repository holds the spec and the
implementation plan only. Nothing is built yet.

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
```

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
