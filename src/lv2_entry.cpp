// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "drizzle.hpp"

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0: return &drizzle::descriptor;
    case 1: return &drizzle::synth_descriptor;
    default: return nullptr;
    }
}
