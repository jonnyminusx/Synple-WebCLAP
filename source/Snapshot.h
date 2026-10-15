#pragma once

#include "Preset.h"
#include "synth/Parameters.h"

#include <array>

namespace synple
{

using ParamValues = std::array<float, kNumParams>;

// Port of synple::Parameters::createSnapshot from the JUCE plugin, operating on plain values.
synth::Parameters createSnapshot(const ParamValues& values, float sampleRate);

float outputGain(const ParamValues& values);

} // namespace synple
