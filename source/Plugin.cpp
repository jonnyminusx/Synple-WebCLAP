#include "ParamTable.h"
#include "Presets.h"
#include "Snapshot.h"
#include "UiResources.h"
#include "dsp/AudioBuffer.h"
#include "midi/MidiLearnMap.h"
#include "synth/Synth.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr const char* kPluginId{"com.jonnyminusx.synple"};
constexpr uint32_t kStateVersion{1};
constexpr size_t kNumParams{synple::kNumParams};

const char* const kFeatures[]{CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor{
    .clap_version = CLAP_VERSION_INIT,
    .id = kPluginId,
    .name = "Synple",
    .vendor = "jonnyminusx",
    .url = "https://github.com/jonnyminusx/Synple",
    .manual_url = "",
    .support_url = "",
    .version = "0.0.1",
    .description = "Subtractive synthesizer",
    .features = kFeatures,
};

std::string jsonEscape(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string number(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

class SynplePlugin
{
  public:
    explicit SynplePlugin(const clap_host_t* host) : host_(host)
    {
        plugin_.desc = &kDescriptor;
        plugin_.plugin_data = this;
        plugin_.init = &SynplePlugin::init;
        plugin_.destroy = &SynplePlugin::destroy;
        plugin_.activate = &SynplePlugin::activate;
        plugin_.deactivate = &SynplePlugin::deactivate;
        plugin_.start_processing = &SynplePlugin::startProcessing;
        plugin_.stop_processing = &SynplePlugin::stopProcessing;
        plugin_.reset = &SynplePlugin::reset;
        plugin_.process = &SynplePlugin::process;
        plugin_.get_extension = &SynplePlugin::getExtension;
        plugin_.on_main_thread = &SynplePlugin::onMainThread;

        loadPresetValues(0);
        for (auto& flag : echoToHost_)
            flag.store(false);
        for (auto& flag : echoToUi_)
            flag.store(false);
    }

    const clap_plugin_t* plugin() const { return &plugin_; }

  private:
    static SynplePlugin& self(const clap_plugin_t* plugin) { return *static_cast<SynplePlugin*>(plugin->plugin_data); }

    //=== clap_plugin ============================================================
    static bool init(const clap_plugin_t* plugin)
    {
        SynplePlugin& s{self(plugin)};
        s.hostParams_ = static_cast<const clap_host_params_t*>(s.host_->get_extension(s.host_, CLAP_EXT_PARAMS));
        s.hostWebview_ = static_cast<const clap_host_webview_t*>(s.host_->get_extension(s.host_, CLAP_EXT_WEBVIEW));
        return true;
    }

    static void destroy(const clap_plugin_t* plugin) { delete &self(plugin); }

    static bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
    {
        SynplePlugin& s{self(plugin)};
        s.sampleRate_ = static_cast<float>(sampleRate);
        s.synth_.allocateResources(s.sampleRate_, static_cast<int>(maxFrames));
        s.paramsChanged_.store(true);
        s.resetSynth();
        return true;
    }

    static void deactivate(const clap_plugin_t* plugin) { self(plugin).synth_.deallocateResources(); }

    static bool startProcessing(const clap_plugin_t*) { return true; }
    static void stopProcessing(const clap_plugin_t*) {}

    static void reset(const clap_plugin_t* plugin)
    {
        SynplePlugin& s{self(plugin)};
        s.paramsChanged_.store(true);
        s.resetSynth();
    }

    static clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
    {
        SynplePlugin& s{self(plugin)};
        s.applyPendingPreset();

        const uint32_t frames{process->frames_count};
        const clap_input_events_t* in{process->in_events};
        const uint32_t numEvents{in->size(in)};

        uint32_t offset{0};
        for (uint32_t i = 0; i < numEvents; ++i)
        {
            const clap_event_header_t* header{in->get(in, i)};
            const uint32_t time{std::min(header->time, frames)};
            if (time > offset)
            {
                s.render(process, offset, time - offset);
                offset = time;
            }
            s.handleEvent(header);
        }
        s.render(process, offset, frames - offset);

        s.flushEchoes(process->out_events);
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* getExtension(const clap_plugin_t*, const char* id)
    {
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
            return &kAudioPorts;
        if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
            return &kNotePorts;
        if (!std::strcmp(id, CLAP_EXT_PARAMS))
            return &kParams;
        if (!std::strcmp(id, CLAP_EXT_STATE))
            return &kState;
        if (!std::strcmp(id, CLAP_EXT_WEBVIEW))
            return &kWebview;
        return nullptr;
    }

    static void onMainThread(const clap_plugin_t* plugin) { self(plugin).sendPendingUiUpdates(); }

    //=== clap_plugin_audio_ports / note_ports ===================================
    static uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }

    static bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
    {
        if (isInput || index != 0)
            return false;
        *info = {};
        info->id = 0;
        std::strncpy(info->name, "Output", sizeof(info->name) - 1);
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    static uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }

    static bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
    {
        if (!isInput || index != 0)
            return false;
        *info = {};
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::strncpy(info->name, "Notes", sizeof(info->name) - 1);
        return true;
    }

    //=== clap_plugin_params =====================================================
    static uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(kNumParams); }

    static bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
    {
        if (index >= kNumParams)
            return false;
        const synple::ParamDef& def{synple::paramDefs()[index]};
        *info = {};
        info->id = index;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        if (def.isChoice())
            info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        std::strncpy(info->name, def.name, sizeof(info->name) - 1);
        info->min_value = def.min;
        info->max_value = def.max;
        info->default_value = def.defaultValue;
        return true;
    }

    static bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
    {
        if (id >= kNumParams)
            return false;
        *value = static_cast<double>(self(plugin).values_[id].load());
        return true;
    }

    static bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t capacity)
    {
        if (id >= kNumParams)
            return false;
        const synple::ParamDef& def{synple::paramDefs()[id]};
        if (def.isChoice())
        {
            const size_t choice{std::min(static_cast<size_t>(std::lround(value)), def.numChoices - 1)};
            std::snprintf(out, capacity, "%s", def.choices[choice]);
        }
        else if (std::strcmp(def.id, "lfoRate") == 0)
        {
            std::snprintf(out, capacity, "%.3f Hz", std::exp(7.0 * value - 4.0));
        }
        else
        {
            std::snprintf(out, capacity, "%.1f %s", value, def.label);
        }
        return true;
    }

    static bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* value)
    {
        if (id >= kNumParams)
            return false;
        const synple::ParamDef& def{synple::paramDefs()[id]};
        if (def.isChoice())
        {
            for (size_t i = 0; i < def.numChoices; ++i)
            {
                if (std::strcmp(def.choices[i], text) == 0)
                {
                    *value = static_cast<double>(i);
                    return true;
                }
            }
        }
        char* end{};
        const double parsed{strtod(text, &end)};
        if (end == text)
            return false;
        *value = parsed;
        return true;
    }

    static void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
    {
        SynplePlugin& s{self(plugin)};
        s.applyPendingPreset();
        for (uint32_t i = 0; i < in->size(in); ++i)
            s.handleEvent(in->get(in, i));
        s.flushEchoes(out);
    }

    //=== clap_plugin_state ======================================================
    static bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
    {
        SynplePlugin& s{self(plugin)};
        std::vector<char> bytes;
        auto append = [&](const void* data, size_t size) {
            const char* p{static_cast<const char*>(data)};
            bytes.insert(bytes.end(), p, p + size);
        };
        append(&kStateVersion, sizeof(kStateVersion));
        for (const auto& value : s.values_)
        {
            const float v{value.load()};
            append(&v, sizeof(v));
        }
        for (size_t i = 0; i < kNumParams; ++i)
        {
            const uint8_t cc{s.learnMap_.ccAtIndex(i)};
            append(&cc, sizeof(cc));
        }

        size_t written{0};
        while (written < bytes.size())
        {
            const int64_t result{stream->write(stream, bytes.data() + written, bytes.size() - written)};
            if (result <= 0)
                return false;
            written += static_cast<size_t>(result);
        }
        return true;
    }

    static bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
    {
        SynplePlugin& s{self(plugin)};
        constexpr size_t kExpected{sizeof(uint32_t) + kNumParams * sizeof(float) + kNumParams};
        std::vector<char> bytes(kExpected);
        size_t total{0};
        while (total < kExpected)
        {
            const int64_t result{stream->read(stream, bytes.data() + total, kExpected - total)};
            if (result <= 0)
                break;
            total += static_cast<size_t>(result);
        }
        if (total < kExpected)
            return false;

        uint32_t version{};
        std::memcpy(&version, bytes.data(), sizeof(version));
        if (version != kStateVersion)
            return false;

        const char* cursor{bytes.data() + sizeof(version)};
        for (size_t i = 0; i < kNumParams; ++i)
        {
            float v{};
            std::memcpy(&v, cursor, sizeof(v));
            cursor += sizeof(v);
            s.setValue(i, s.clampAndSnap(i, v), true);
        }
        for (size_t i = 0; i < kNumParams; ++i)
        {
            const auto cc{static_cast<uint8_t>(*cursor++)};
            if (cc <= 127)
                s.learnMap_.setCC(i, cc);
        }
        s.paramsChanged_.store(true);
        s.learnDirty_.store(true);
        for (auto& flag : s.echoToUi_)
            flag.store(true);
        s.host_->request_callback(s.host_);
        return true;
    }

    //=== clap_plugin_webview ====================================================
    static int32_t webviewGetUri(const clap_plugin_t*, char* uri, uint32_t capacity)
    {
        constexpr char kUri[]{"/index.html"};
        if (capacity > 0 && uri != nullptr)
            std::snprintf(uri, capacity, "%s", kUri);
        return static_cast<int32_t>(sizeof(kUri));
    }

    static bool webviewGetResource(const clap_plugin_t*,
                                   const char* path,
                                   char* mime,
                                   uint32_t mimeCapacity,
                                   const clap_ostream_t* stream)
    {
        if (std::strcmp(path, "/index.html") != 0 && std::strcmp(path, "/") != 0)
            return false;
        std::snprintf(mime, mimeCapacity, "text/html");

        size_t written{0};
        while (written < synple::kUiHtmlSize)
        {
            const int64_t result{stream->write(stream, synple::kUiHtml + written, synple::kUiHtmlSize - written)};
            if (result <= 0)
                return false;
            written += static_cast<size_t>(result);
        }
        return true;
    }

    static bool webviewReceive(const clap_plugin_t* plugin, const void* buffer, uint32_t size)
    {
        return self(plugin).handleUiCommand(std::string_view(static_cast<const char*>(buffer), size));
    }

    //=== parameters ============================================================
    float clampAndSnap(size_t index, float value) const
    {
        const synple::ParamDef& def{synple::paramDefs()[index]};
        float v{std::clamp(value, def.min, def.max)};
        if (def.interval > 0.0f)
            v = def.min + std::round((v - def.min) / def.interval) * def.interval;
        return std::clamp(v, def.min, def.max);
    }

    void setValue(size_t index, float value, bool echoToHost)
    {
        values_[index].store(value);
        paramsChanged_.store(true);
        if (echoToHost)
            echoToHost_[index].store(true);
    }

    void setValueFromAudioThread(size_t index, float value, bool echoToHost)
    {
        setValue(index, value, echoToHost);
        echoToUi_[index].store(true);
        host_->request_callback(host_);
    }

    synple::ParamValues valuesSnapshot() const
    {
        synple::ParamValues out;
        for (size_t i = 0; i < kNumParams; ++i)
            out[i] = values_[i].load();
        return out;
    }

    void loadPresetValues(size_t index)
    {
        const synple::Preset& preset{presets_[index]};
        for (size_t i = 0; i < kNumParams; ++i)
            setValue(i, preset.parameters()[i], true);
        currentPreset_.store(static_cast<int>(index));
    }

    void applyPendingPreset()
    {
        const int pending{pendingPreset_.exchange(-1)};
        if (pending < 0 || static_cast<size_t>(pending) >= presets_.size())
            return;
        applyPresetFromAudioThread(static_cast<size_t>(pending));
    }

    void applyPresetFromAudioThread(size_t index)
    {
        loadPresetValues(index);
        for (auto& flag : echoToUi_)
            flag.store(true);
        presetDirty_.store(true);
        host_->request_callback(host_);
        resetSynth();
    }

    void resetSynth()
    {
        synth_.reset();
        synth_.setOutputLevelInstantly(synple::outputGain(valuesSnapshot()));
    }

    //=== audio =================================================================
    void render(const clap_process_t* process, uint32_t offset, uint32_t count)
    {
        if (count == 0 || process->audio_outputs_count == 0)
            return;

        if (paramsChanged_.exchange(false))
            synth_.setParameters(synple::createSnapshot(valuesSnapshot(), sampleRate_));

        const clap_audio_buffer_t& out{process->audio_outputs[0]};
        std::vector<std::span<float>> channels;
        channels.reserve(out.channel_count);
        for (uint32_t ch = 0; ch < out.channel_count; ++ch)
            channels.emplace_back(out.data32[ch] + offset, count);
        dsp::AudioBuffer buffer{std::move(channels)};
        synth_.render(buffer);
    }

    void handleEvent(const clap_event_header_t* header)
    {
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        switch (header->type)
        {
        case CLAP_EVENT_NOTE_ON:
        {
            const auto* e{reinterpret_cast<const clap_event_note_t*>(header)};
            const auto velocity{static_cast<uint8_t>(std::clamp(std::lround(e->velocity * 127.0), 1L, 127L))};
            handleMidi(static_cast<uint8_t>(0x90 | (e->channel & 0x0F)), static_cast<uint8_t>(e->key & 0x7F), velocity);
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        {
            const auto* e{reinterpret_cast<const clap_event_note_t*>(header)};
            handleMidi(static_cast<uint8_t>(0x80 | (e->channel & 0x0F)), static_cast<uint8_t>(e->key & 0x7F), 0);
            break;
        }
        case CLAP_EVENT_MIDI:
        {
            const auto* e{reinterpret_cast<const clap_event_midi_t*>(header)};
            handleMidi(e->data[0], e->data[1], e->data[2]);
            break;
        }
        case CLAP_EVENT_PARAM_VALUE:
        {
            const auto* e{reinterpret_cast<const clap_event_param_value_t*>(header)};
            if (e->param_id < kNumParams)
                setValueFromAudioThread(e->param_id, clampAndSnap(e->param_id, static_cast<float>(e->value)), false);
            break;
        }
        default:
            break;
        }
    }

    void handleMidi(uint8_t data0, uint8_t data1, uint8_t data2)
    {
        if ((data0 & 0xF0) == 0xB0)
        {
            const uint8_t controller{static_cast<uint8_t>(data1 & 0x7F)};
            const float normalised{static_cast<float>(data2 & 0x7F) / 127.0f};

            const int captured{learnMap_.tryCaptureLearning(controller)};
            if (captured >= 0)
            {
                setFromNormalised(static_cast<size_t>(captured), normalised);
                learnDirty_.store(true);
                host_->request_callback(host_);
            }

            for (size_t i = 0; i < midi::MidiLearnMap::kNumParams; ++i)
            {
                if (learnMap_.ccAtIndex(i) == controller)
                    setFromNormalised(i, normalised);
            }
        }

        if ((data0 & 0xF0) == 0xC0 && data1 < presets_.size())
            applyPresetFromAudioThread(data1);

        synth_.midiProcessor().process(data0, data1, data2);
    }

    void setFromNormalised(size_t index, float normalised)
    {
        const synple::ParamDef& def{synple::paramDefs()[index]};
        const float value{def.min + (def.max - def.min) * std::pow(normalised, 1.0f / def.skew)};
        setValueFromAudioThread(index, clampAndSnap(index, value), true);
    }

    void flushEchoes(const clap_output_events_t* out)
    {
        for (size_t i = 0; i < kNumParams; ++i)
        {
            if (!echoToHost_[i].exchange(false))
                continue;
            clap_event_param_value_t event{};
            event.header.size = sizeof(event);
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.param_id = static_cast<clap_id>(i);
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = static_cast<double>(values_[i].load());
            out->try_push(out, &event.header);
        }
    }

    //=== webview ===============================================================
    void send(const std::string& json) const
    {
        if (hostWebview_ != nullptr)
            hostWebview_->send(host_, json.data(), static_cast<uint32_t>(json.size()));
    }

    std::string learnJson() const
    {
        std::string json{"{\"learningParam\":\""};
        const char* learning{learnMap_.learningParamId()};
        json += learning != nullptr ? learning : "";
        json += "\",\"assignments\":{";
        bool first{true};
        learnMap_.forEachAssignment([&](const char* id, uint8_t cc) {
            if (!first)
                json += ",";
            first = false;
            json += "\"" + std::string(id) + "\":" + std::to_string(cc);
        });
        json += "}}";
        return json;
    }

    void sendInit() const
    {
        std::string json{"{\"t\":\"init\",\"params\":["};
        for (size_t i = 0; i < kNumParams; ++i)
        {
            const synple::ParamDef& def{synple::paramDefs()[i]};
            if (i > 0)
                json += ",";
            json += "{\"id\":\"" + std::string(def.id) + "\",\"name\":\"" + jsonEscape(def.name) + "\",\"label\":\"" +
                    jsonEscape(def.label) + "\",\"start\":" + number(def.min) + ",\"end\":" + number(def.max) +
                    ",\"skew\":" + number(def.skew) + ",\"interval\":" + number(def.interval) + ",\"choices\":";
            if (def.isChoice())
            {
                json += "[";
                for (size_t c = 0; c < def.numChoices; ++c)
                    json += std::string(c > 0 ? "," : "") + "\"" + jsonEscape(def.choices[c]) + "\"";
                json += "]";
            }
            else
            {
                json += "null";
            }
            json += ",\"value\":" + number(values_[i].load()) + "}";
        }
        json += "],\"presets\":[";
        for (size_t i = 0; i < presets_.size(); ++i)
            json += std::string(i > 0 ? "," : "") + "\"" + jsonEscape(presets_[i].name()) + "\"";
        json += "],\"preset\":" + std::to_string(currentPreset_.load()) + ",\"learn\":" + learnJson() + "}";
        send(json);
    }

    void sendPendingUiUpdates()
    {
        for (size_t i = 0; i < kNumParams; ++i)
        {
            if (echoToUi_[i].exchange(false))
                send("{\"t\":\"p\",\"i\":" + std::to_string(i) + ",\"v\":" + number(values_[i].load()) + "}");
        }
        if (presetDirty_.exchange(false))
            send("{\"t\":\"preset\",\"preset\":" + std::to_string(currentPreset_.load()) + "}");
        if (learnDirty_.exchange(false))
            send("{\"t\":\"learn\",\"learn\":" + learnJson() + "}");
    }

    bool handleUiCommand(std::string_view command)
    {
        const size_t space{command.find(' ')};
        const std::string_view name{command.substr(0, space)};
        const std::string argument{space == std::string_view::npos ? "" : std::string(command.substr(space + 1))};

        if (name == "ready")
        {
            sendInit();
        }
        else if (name == "set")
        {
            size_t index{};
            float value{};
            if (std::sscanf(argument.c_str(), "%zu %f", &index, &value) != 2 || index >= kNumParams)
                return false;
            setValue(index, clampAndSnap(index, value), true);
            if (hostParams_ != nullptr)
                hostParams_->request_flush(host_);
        }
        else if (name == "preset")
        {
            const int index{atoi(argument.c_str())};
            if (index < 0 || static_cast<size_t>(index) >= presets_.size())
                return false;
            pendingPreset_.store(index);
            if (hostParams_ != nullptr)
                hostParams_->request_flush(host_);
        }
        else if (name == "learnStart")
        {
            learnMap_.beginLearn(argument);
            learnDirty_.store(true);
            sendPendingUiUpdates();
        }
        else if (name == "learnCancel")
        {
            learnMap_.cancelLearn();
            learnDirty_.store(true);
            sendPendingUiUpdates();
        }
        else if (name == "learnClear")
        {
            learnMap_.clearLearn(argument);
            learnDirty_.store(true);
            sendPendingUiUpdates();
        }
        else
        {
            return false;
        }
        return true;
    }

    static inline const clap_plugin_audio_ports_t kAudioPorts{&audioPortsCount, &audioPortsGet};
    static inline const clap_plugin_note_ports_t kNotePorts{&notePortsCount, &notePortsGet};
    static inline const clap_plugin_params_t kParams{&paramsCount,
                                                      &paramsGetInfo,
                                                      &paramsGetValue,
                                                      &paramsValueToText,
                                                      &paramsTextToValue,
                                                      &paramsFlush};
    static inline const clap_plugin_state_t kState{&stateSave, &stateLoad};
    static inline const clap_plugin_webview_t kWebview{&webviewGetUri, &webviewGetResource, &webviewReceive};

    clap_plugin_t plugin_{};
    const clap_host_t* host_;
    const clap_host_params_t* hostParams_{};
    const clap_host_webview_t* hostWebview_{};

    synth::Synth synth_;
    midi::MidiLearnMap learnMap_;
    synple::Presets presets_;
    float sampleRate_{44100.0f};

    std::array<std::atomic<float>, kNumParams> values_{};
    std::array<std::atomic<bool>, kNumParams> echoToHost_{};
    std::array<std::atomic<bool>, kNumParams> echoToUi_{};
    std::atomic<bool> paramsChanged_{true};
    std::atomic<bool> learnDirty_{false};
    std::atomic<bool> presetDirty_{false};
    std::atomic<int> pendingPreset_{-1};
    std::atomic<int> currentPreset_{0};
};

const clap_plugin_t* createPlugin(const clap_host_t* host)
{
    return (new SynplePlugin(host))->plugin();
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*)
{
    return 1;
}

const clap_plugin_descriptor_t* factoryGetDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, kPluginId) != 0)
        return nullptr;
    return createPlugin(host);
}

const clap_plugin_factory_t kFactory{&factoryGetPluginCount, &factoryGetDescriptor, &factoryCreate};

bool entryInit(const char*)
{
    return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION_INIT, &entryInit, &entryDeinit, &entryGetFactory};
