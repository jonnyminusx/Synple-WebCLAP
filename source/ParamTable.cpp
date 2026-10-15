#include "ParamTable.h"

namespace synple
{

namespace
{

constexpr ParamDef knob(const char* id,
                        const char* name,
                        const char* label,
                        float min,
                        float max,
                        float interval,
                        float defaultValue,
                        float skew = 1.0f)
{
    return {id, name, label, min, max, interval, defaultValue, skew, {}, 0};
}

constexpr ParamDef choice(const char* id,
                          const char* name,
                          std::array<const char*, 3> choices,
                          std::size_t numChoices,
                          float defaultValue)
{
    return {id, name, "", 0.0f, static_cast<float>(numChoices - 1), 1.0f, defaultValue, 1.0f, choices, numChoices};
}

constexpr std::array<const char*, 3> kWaveforms{"Sawtooth", "Sine", "Pulse"};

const std::array<ParamDef, kNumParams> kParams{{
    choice("glideMode", "Glide Mode", {"Off", "Legato", "Always"}, 3, 0.0f),
    knob("glideRate", "Glide Rate", "%", 0.0f, 100.0f, 1.0f, 35.0f),
    knob("glideBend", "Glide Bend", "semi", -36.0f, 36.0f, 0.01f, 0.0f, 0.4f),
    knob("filterFreq", "Filter Freq", "%", 0.0f, 100.0f, 0.1f, 100.0f),
    knob("filterReso", "Filter Reso", "%", 0.0f, 100.0f, 1.0f, 15.0f),
    knob("filterEnv", "Filter Env", "%", -100.0f, 100.0f, 0.1f, 50.0f),
    knob("filterLFO", "Filter LFO", "%", 0.0f, 100.0f, 1.0f, 0.0f),
    knob("filterVelocity", "Velocity", "%", -100.0f, 100.0f, 1.0f, 0.0f),
    knob("filterAttack", "Filter Attack", "%", 0.0f, 100.0f, 1.0f, 0.0f),
    knob("filterDecay", "Filter Decay", "%", 0.0f, 100.0f, 1.0f, 30.0f),
    knob("filterSustain", "Filter Sustain", "%", 0.0f, 100.0f, 1.0f, 0.0f),
    knob("filterRelease", "Filter Release", "%", 0.0f, 100.0f, 1.0f, 25.0f),
    knob("envAttack", "Env Attack", "%", 0.0f, 100.0f, 1.0f, 0.0f),
    knob("envDecay", "Env Decay", "%", 0.0f, 100.0f, 1.0f, 50.0f),
    knob("envSustain", "Env Sustain", "%", 0.0f, 100.0f, 1.0f, 100.0f),
    knob("envRelease", "Env Release", "%", 0.0f, 100.0f, 1.0f, 30.0f),
    knob("lfoRate", "LFO Rate", "Hz", 0.0f, 1.0f, 0.0f, 0.81f),
    knob("vibrato", "Vibrato", "%", 0.0f, 100.0f, 0.1f, 0.0f),
    knob("noise", "Noise", "%", 0.0f, 100.0f, 1.0f, 0.0f),
    knob("octave", "Octave", "", -2.0f, 2.0f, 1.0f, 0.0f),
    knob("tuning", "Tuning", "cent", -100.0f, 100.0f, 0.1f, 0.0f),
    knob("outputLevel", "Output Level", "dB", -24.0f, 6.0f, 0.1f, 0.0f),
    choice("polyMode", "Polyphony", {"Mono", "Poly", ""}, 2, 1.0f),
    knob("pwmDepth", "PWM Depth", "%", 0.0f, 100.0f, 0.1f, 0.0f),
    knob("osc1Volume", "Osc 1 Volume", "%", 0.0f, 100.0f, 0.1f, 100.0f),
    knob("osc2Volume", "Osc 2 Volume", "%", 0.0f, 100.0f, 0.1f, 0.0f),
    knob("osc1Tune", "Osc 1 Tune", "semi", -24.0f, 24.0f, 1.0f, 0.0f),
    knob("osc2Tune", "Osc 2 Tune", "semi", -24.0f, 24.0f, 1.0f, 0.0f),
    knob("osc1Fine", "Osc 1 Fine", "cent", -50.0f, 50.0f, 0.1f, 0.0f, 0.3f),
    knob("osc2Fine", "Osc 2 Fine", "cent", -50.0f, 50.0f, 0.1f, 0.0f, 0.3f),
    choice("osc1Waveform", "Osc 1 Waveform", kWaveforms, 3, 0.0f),
    choice("osc2Waveform", "Osc 2 Waveform", kWaveforms, 3, 0.0f),
    knob("osc1PulseWidth", "Osc 1 Pulse Width", "%", 10.0f, 90.0f, 0.1f, 50.0f),
    knob("osc2PulseWidth", "Osc 2 Pulse Width", "%", 10.0f, 90.0f, 0.1f, 50.0f),
}};

} // namespace

const std::array<ParamDef, kNumParams>& paramDefs()
{
    return kParams;
}

} // namespace synple
