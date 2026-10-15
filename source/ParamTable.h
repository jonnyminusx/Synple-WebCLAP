#pragma once

#include "Preset.h"

#include <array>
#include <cstddef>

namespace synple
{

struct ParamDef
{
    const char* id;
    const char* name;
    const char* label;
    float min;
    float max;
    float interval;
    float defaultValue;
    float skew;
    std::array<const char*, 3> choices;
    std::size_t numChoices;

    bool isChoice() const { return numChoices > 0; }
};

// Canonical order shared with the presets and midi::MidiLearnMap::kParamIds. The CLAP param id is the index.
const std::array<ParamDef, kNumParams>& paramDefs();

} // namespace synple
