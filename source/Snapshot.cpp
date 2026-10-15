#include "Snapshot.h"

#include "synth/Synth.h"

#include <cmath>
#include <numbers>

namespace synple
{

namespace
{

enum ParamIndex : std::size_t
{
    kGlideMode,
    kGlideRate,
    kGlideBend,
    kFilterFreq,
    kFilterReso,
    kFilterEnv,
    kFilterLfo,
    kFilterVelocity,
    kFilterAttack,
    kFilterDecay,
    kFilterSustain,
    kFilterRelease,
    kEnvAttack,
    kEnvDecay,
    kEnvSustain,
    kEnvRelease,
    kLfoRate,
    kVibrato,
    kNoise,
    kOctave,
    kTuning,
    kOutputLevel,
    kPolyMode,
    kPwmDepth,
    kOsc1Volume,
    kOsc2Volume,
    kOsc1Tune,
    kOsc2Tune,
    kOsc1Fine,
    kOsc2Fine,
    kOsc1Waveform,
    kOsc2Waveform,
    kOsc1PulseWidth,
    kOsc2PulseWidth,
};

constexpr float kSemitoneRatio{1.0594630943592952f}; // 2^(1/12)

float multiplierFromParam(float rateScale, float paramValue)
{
    return std::exp(-rateScale * std::exp(5.5f - 0.075f * paramValue));
}

float tuneFactor(float semitones, float cents)
{
    return std::pow(kSemitoneRatio, -(semitones + 0.01f * cents));
}

int choiceIndex(float value)
{
    return static_cast<int>(std::lround(value));
}

} // namespace

float outputGain(const ParamValues& values)
{
    return std::pow(10.0f, values[kOutputLevel] * 0.05f);
}

synth::Parameters createSnapshot(const ParamValues& v, const float sampleRate)
{
    const float updateInterval{static_cast<float>(synth::Synth::lfoMaxSamplesPerUpdate())};
    const float inverseSampleRate{1.0f / sampleRate};
    const float inverseUpdateRate{inverseSampleRate * updateInterval};

    const float resonance{v[kFilterReso] / 100.0f};
    const bool ignoreVelocity{v[kFilterVelocity] < -90.0f};

    const float normalisedNoise{v[kNoise] / 100.0f};
    const float noiseMix{normalisedNoise * normalisedNoise * 0.06f};

    const float filterLfoNormalised{v[kFilterLfo] / 100.0f};
    const float rawVibrato{v[kVibrato] / 200.0f};

    const float tuneInSemi{-36.3763f - 12.0f * v[kOctave] - v[kTuning] / 100.0f};
    const float lfoHz{std::exp(7.0f * v[kLfoRate] - 4.0f)};

    synth::Parameters p;

    p.oscillator.globalTune = sampleRate * std::exp(0.05776226505f * tuneInSemi);
    p.oscillator.osc1.tune = tuneFactor(v[kOsc1Tune], v[kOsc1Fine]);
    p.oscillator.osc2.tune = tuneFactor(v[kOsc2Tune], v[kOsc2Fine]);
    p.oscillator.osc1.volume = v[kOsc1Volume] / 100.0f;
    p.oscillator.osc2.volume = v[kOsc2Volume] / 100.0f;
    p.oscillator.noiseMix = noiseMix;
    p.oscillator.osc1.pulseWidth = v[kOsc1PulseWidth] / 100.0f;
    p.oscillator.osc2.pulseWidth = v[kOsc2PulseWidth] / 100.0f;
    p.oscillator.osc1.waveform = static_cast<synth::WaveformType>(choiceIndex(v[kOsc1Waveform]));
    p.oscillator.osc2.waveform = static_cast<synth::WaveformType>(choiceIndex(v[kOsc2Waveform]));

    p.filter.keyTracking = (0.08f * v[kFilterFreq]) - 1.5f;
    p.filter.q = std::exp(3.0f * resonance);
    p.filter.lfoDepth = 2.5f * filterLfoNormalised * filterLfoNormalised;
    p.filter.envelopeDepth = 0.06f * v[kFilterEnv];
    p.filter.velocitySensitivity = ignoreVelocity ? 0.0f : v[kFilterVelocity] * 0.0005f;

    const float filterSustain{v[kFilterSustain] / 100.0f};
    p.filter.envelope = synth::Parameters::ADSR{multiplierFromParam(inverseUpdateRate, v[kFilterAttack]),
                                                multiplierFromParam(inverseUpdateRate, v[kFilterDecay]),
                                                filterSustain * filterSustain,
                                                multiplierFromParam(inverseUpdateRate, v[kFilterRelease])};

    p.lfo.increment = lfoHz * inverseUpdateRate * (2.0f * std::numbers::pi_v<float>);
    p.lfo.vibratoAmount = 0.2f * rawVibrato * rawVibrato;
    p.lfo.pwmDepth = v[kPwmDepth] / 100.0f;

    p.glide.mode = static_cast<synth::GlideMode>(choiceIndex(v[kGlideMode]));
    p.glide.rateCoefficient =
        v[kGlideRate] < 2.0f ? 1.0f : 1.0f - std::exp(-inverseUpdateRate * std::exp(6.0f - 0.07f * v[kGlideRate]));
    p.glide.bendSemitones = v[kGlideBend];

    p.output.gain = outputGain(v);
    p.output.volumeTrim = 0.0008f * (3.2f - 25.0f * noiseMix) * (1.5f - 0.5f * resonance);
    p.output.polyphonic = choiceIndex(v[kPolyMode]) == 1;
    p.output.ignoreVelocity = ignoreVelocity;

    const float release{v[kEnvRelease] < 1.0f ? 0.75f : multiplierFromParam(inverseSampleRate, v[kEnvRelease])};
    p.envelope = synth::Parameters::ADSR{multiplierFromParam(inverseSampleRate, v[kEnvAttack]),
                                         multiplierFromParam(inverseSampleRate, v[kEnvDecay]),
                                         v[kEnvSustain] / 100.0f,
                                         release};

    return p;
}

} // namespace synple
