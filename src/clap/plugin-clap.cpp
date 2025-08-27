/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#include "configuration.h"
#include <clap/clap.h>
#include <chrono>

#include <clap/helpers/plugin.hh>
#include "engine/engine.h"
#include "presets/preset-manager.h"

#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hxx>

#include <memory>
#include "sst/plugininfra/patch-support/patch_base_clap_adapter.h"
#include "sst/plugininfra/cpufeatures.h"

#include "sst/voicemanager/midi1_to_voicemanager.h"
#include "sst/clap_juce_shim/clap_juce_shim.h"

#include "ui/plugin-editor.h"

#include <clapwrapper/vst3.h>

namespace baconpaul::twofilters
{

extern const clap_plugin_descriptor *getDescriptor();

namespace clapimpl
{

static constexpr clap::helpers::MisbehaviourHandler misLevel =
    clap::helpers::MisbehaviourHandler::Ignore;
static constexpr clap::helpers::CheckingLevel checkLevel = clap::helpers::CheckingLevel::Maximal;

using plugHelper_t = clap::helpers::Plugin<misLevel, checkLevel>;

struct TwoFilters : public plugHelper_t, sst::clap_juce_shim::EditorProvider
{
    TwoFilters(const clap_host *h) : plugHelper_t(getDescriptor(), h)
    {
        engine = std::make_unique<Engine>();

        engine->clapHost = h;

        clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
        clapJuceShim->setResizable(true);
    }
    virtual ~TwoFilters(){};

    std::unique_ptr<Engine> engine;
    size_t blockPos{0};

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        engine->setSampleRate(sampleRate);
        return true;
    }

    void onMainThread() noexcept override { engine->onMainThread(); }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        info->id = 75241 + index + (isInput ? 73 : 951);
        info->in_place_pair = CLAP_INVALID_ID;
        if (isInput)
            strncpy(info->name, "Main Input", sizeof(info->name));
        else
            strncpy(info->name, "Main Out", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    bool implementsAudioPortsActivation() const noexcept override { return true; }
    bool audioPortsActivationCanActivateWhileProcessing() const noexcept override { return true; }
    bool audioPortsActivationSetActive(bool is_input, uint32_t port_index, bool is_active,
                                       uint32_t sample_size) noexcept override
    {
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return 0; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        return false;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();

        auto ev = process->in_events;
        auto outq = process->out_events;
        auto sz = ev->size(ev);

        const clap_event_header_t *nextEvent{nullptr};
        uint32_t nextEventIndex{0};
        if (sz != 0)
        {
            nextEvent = ev->get(ev, nextEventIndex);
        }

        if (process->transport)
        {
            // engine->monoValues.tempoSyncRatio = process->transport->tempo / 120.0;
        }
        else
        {
            // engine->monoValues.tempoSyncRatio = 1.f;
        }

        static constexpr int outBus{1};
        static constexpr int outChan{2};
        float *out[outChan];
        for (auto i = 0; i < outBus; ++i)
        {
            auto lo = process->audio_outputs[i].data32;
            out[2 * i] = lo[0];
            out[2 * i + 1] = lo[1];
        }

        for (auto s = 0U; s < process->frames_count; ++s)
        {
            if (blockPos == 0)
            {
                // Only realy need to run events when we do the block process
                while (nextEvent && nextEvent->time <= s)
                {
                    handleEvent(nextEvent);
                    nextEventIndex++;
                    if (nextEventIndex < sz)
                        nextEvent = ev->get(ev, nextEventIndex);
                    else
                        nextEvent = nullptr;
                }

                engine->process(outq);
            }

            for (auto i = 0; i < outChan; ++i)
            {
                out[i][s] = engine->output[i][blockPos];
            }

            blockPos++;
            if (blockPos == blockSize)
            {
                blockPos = 0;
            }
        }

        while (nextEvent)
        {
            handleEvent(nextEvent);
            nextEventIndex++;
            if (nextEventIndex < sz)
                nextEvent = ev->get(ev, nextEventIndex);
            else
                nextEvent = nullptr;
        }
        return CLAP_PROCESS_CONTINUE;
    }

    void reset() noexcept override {}

    bool handleEvent(const clap_event_header_t *nextEvent)
    {
        if (nextEvent->space_id == CLAP_CORE_EVENT_SPACE_ID)
        {
            switch (nextEvent->type)
            {
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto pevt = reinterpret_cast<const clap_event_param_value *>(nextEvent);
                auto par =
                    sst::plugininfra::patch_support::paramFromClapEvent<Param>(pevt, engine->patch);
                if (par)
                {
                    engine->handleParamValue(par, pevt->param_id, pevt->value);
                }
            }
            break;

            default:
            {
                SQLOG("Unknown inbound event of type " << nextEvent->type);
            }
            break;
            }
        }
        return true;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *ostream) noexcept override
    {
        engine->mainToAudio.push({Engine::MainToAudioMsg::SEND_PREP_FOR_STREAM});
        if (_host.canUseParams())
            _host.paramsRequestFlush();

        // best efforts on that message for now
        static constexpr int maxIts{5};
        int i{0};
        for (i = 0; i < maxIts && !engine->readyForStream; ++i)
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(4ms);
        }
        if (i == maxIts && !engine->readyForStream)
        {
            // sigh. something is wonky
            engine->prepForStream();
        }

        auto res = sst::plugininfra::patch_support::patchToOutStream(engine->patch, ostream);
        engine->readyForStream = false;

        return res;
    }

    bool stateLoad(const clap_istream *istream) noexcept override
    {
        Patch patchCopy;
        if (!sst::plugininfra::patch_support::inStreamToPatch(istream, patchCopy))
            return false;

        presets::PresetManager::sendEntirePatchToAudio(patchCopy, engine->mainToAudio,
                                                       patchCopy.name, _host.host());
        if (_host.canUseParams())
        {
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
            _host.paramsRequestFlush();
        }
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return engine->patch.params.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsInfo(paramIndex, info, engine->patch);
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsValue(paramId, value, engine->patch);
    }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsValueToText(paramId, value, display,
                                                                       size, engine->patch);
    }
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsTextToValue(paramId, display, value,
                                                                       engine->patch);
    }
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        auto sz = in->size(in);

        for (int i = 0; i < sz; ++i)
        {
            const clap_event_header_t *nextEvent{nullptr};
            nextEvent = in->get(in, i);
            handleEvent(nextEvent);
        }

        engine->processUIQueue(out);
    }

  public:
    bool implementsGui() const noexcept override { return clapJuceShim != nullptr; }
    std::unique_ptr<sst::clap_juce_shim::ClapJuceShim> clapJuceShim;
    ADD_SHIM_IMPLEMENTATION(clapJuceShim)
    ADD_SHIM_LINUX_TIMER(clapJuceShim)
    std::unique_ptr<juce::Component> createEditor() override
    {
        auto res = std::make_unique<baconpaul::twofilters::ui::PluginEditor>(
            engine->audioToUi, engine->mainToAudio, _host.host());

        res->onZoomChanged = [this](auto f)
        {
            if (_host.canUseGui() && clapJuceShim->isEditorAttached())
            {
                // SQLOG("onZoomChanged " << f);
                auto s = f * clapJuceShim->getGuiScale();
                guiSetSize(baconpaul::twofilters::ui::PluginEditor::edWidth * s,
                           baconpaul::twofilters::ui::PluginEditor::edHeight * s);
                _host.guiRequestResize(baconpaul::twofilters::ui::PluginEditor::edWidth * s,
                                       baconpaul::twofilters::ui::PluginEditor::edHeight * s);
            }
        };

        onShow = [e = res.get()]()
        {
            // SQLOG("onShow with zoom factor " << e->zoomFactor);
            e->setZoomFactor(e->zoomFactor);
            return true;
        };
        // res->sneakyStartupGrabFrom(engine->patch);
        res->repaint();

        return res;
    }

    bool registerOrUnregisterTimer(clap_id &id, int ms, bool reg) override
    {
        if (!_host.canUseTimerSupport())
            return false;
        if (reg)
        {
            _host.timerSupportRegister(ms, &id);
        }
        else
        {
            _host.timerSupportUnregister(id);
        }
        return true;
    }

    static uint32_t vst3_getNumMIDIChannels(const clap_plugin *plugin, uint32_t note_port)
    {
        return 16;
    }
    static uint32_t vst3_supportedNoteExpressions(const clap_plugin *plugin)
    {
        return clap_supported_note_expressions::AS_VST3_NOTE_EXPRESSION_TUNING |
               clap_supported_note_expressions::AS_VST3_NOTE_EXPRESSION_PAN;
    }

    const void *extension(const char *id) noexcept override
    {
        if (strcmp(id, CLAP_PLUGIN_AS_VST3) == 0)
        {
            static clap_plugin_as_vst3 v3p{vst3_getNumMIDIChannels, vst3_supportedNoteExpressions};
            return &v3p;
        }

        return nullptr;
    }
};

} // namespace clapimpl

const clap_plugin *makePlugin(const clap_host *h)
{
    auto res = new baconpaul::twofilters::clapimpl::TwoFilters(h);
    return res->clapPlugin();
}
} // namespace baconpaul::twofilters

namespace chlp = clap::helpers;
namespace bpss = baconpaul::twofilters::clapimpl;

template class chlp::Plugin<bpss::misLevel, bpss::checkLevel>;
template class chlp::HostProxy<bpss::misLevel, bpss::checkLevel>;
