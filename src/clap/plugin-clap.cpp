/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2026, Paul Walker and Various authors, as described in the github
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

#include <array>
#include <memory>
#include <fstream>
#include <sstream>
#include "sst/plugininfra/patch-support/patch_base_clap_adapter.h"
#include "sst/plugininfra/cpufeatures.h"

#include "sst/clap_juce_shim/clap_juce_shim.h"

#include "sst/basic-blocks/modulators/TransportClapAdapter.h"

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
        buildRemotePages();

        clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
        clapJuceShim->setResizable(true);
    }
    virtual ~TwoFilters() {}

    std::unique_ptr<Engine> engine;
    size_t blockPos{0};

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        // The audio thread is stopped here; seed it from the main-thread source of truth.
        engine->prepareForActivation();
        engine->patch.copyValuesFrom(engine->patchMain);
        engine->setSampleRate(sampleRate);
        return true;
    }

    void onMainThread() noexcept override { engine->onMainThread(); }

    bool implementsAudioPorts() const noexcept override { return true; }
    static constexpr uint32_t sidechainInputIndex{1};

    uint32_t audioPortsCount(bool isInput) const noexcept override { return isInput ? 2 : 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (index >= audioPortsCount(isInput))
            return false;

        info->id = 75241 + index + (isInput ? 73 : 951);
        info->in_place_pair = CLAP_INVALID_ID;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        if (isInput && index == sidechainInputIndex)
        {
            strncpy(info->name, "Sidechain Input", sizeof(info->name));
            info->flags = 0;
        }
        else if (isInput)
            strncpy(info->name, "Main Input", sizeof(info->name));
        else
            strncpy(info->name, "Main Out", sizeof(info->name));
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
        engine->updateOversampling();
        auto useFeedback = engine->patch.routingNode.feedbackPower > 0.5;
        auto useNoise = engine->patch.routingNode.noisePower > 0.5;
        auto useOS = engine->overSampling;
        auto mode = (Engine::RoutingModes)std::round(engine->patch.routingNode.routingMode);

        switch (mode)
        {
#define CWNS(x, vf, vn)                                                                            \
    if (useOS)                                                                                     \
    {                                                                                              \
        processForRouting<x, vf, vn, true>(process);                                               \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        processForRouting<x, vf, vn, false>(process);                                              \
    }

#define CWFB(x, v)                                                                                 \
    if (useNoise)                                                                                  \
    {                                                                                              \
        CWNS(x, v, true);                                                                          \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        CWNS(x, v, false);                                                                         \
    }

#define CSRM(x)                                                                                    \
    case x:                                                                                        \
        if (useFeedback)                                                                           \
        {                                                                                          \
            CWFB(x, true)                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            CWFB(x, false)                                                                         \
        }                                                                                          \
        break;

            CSRM(Engine::RoutingModes::Serial)
            CSRM(Engine::RoutingModes::Parallel_FBOne)
            CSRM(Engine::RoutingModes::Parallel_FBBoth)
            CSRM(Engine::RoutingModes::Parallel_FBEach)
#undef CSRM
#undef CWFB
#undef CWNS
        }

        // a nan poisons the state, so the first sample of a block is enough to spot it
        auto oD = process->audio_outputs[0].data32;
        if (process->frames_count > 0)
        {
            if (!std::isfinite(oD[0][0]) || !std::isfinite(oD[1][0]))
            {
                engine->reinitializeDSP();
                // so the fresh filters get coefficients before the next sample
                blockPos = 0;
            }
            else if (!std::isfinite(engine->envLevel))
            {
                // a bad sidechain should not reset healthy main audio
                engine->reinitializeFollower();
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }

    template <Engine::RoutingModes routingMode, bool withFeedback, bool withNoise, bool withOS>
    clap_process_status processForRouting(const clap_process *process) noexcept
    {
        auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();

        sst::basic_blocks::modulators::fromClapTransport(engine->transport, process->transport);

        auto ev = process->in_events;
        auto outq = process->out_events;
        auto sz = ev->size(ev);

        const clap_event_header_t *nextEvent{nullptr};
        uint32_t nextEventIndex{0};
        if (sz != 0)
        {
            nextEvent = ev->get(ev, nextEventIndex);
        }

        auto inD = process->audio_inputs->data32;
        auto outD = process->audio_outputs->data32;

        // hosts may omit the sidechain or hand it over mono
        const float *scL{nullptr}, *scR{nullptr};
        if (process->audio_inputs_count > sidechainInputIndex)
        {
            auto &sc = process->audio_inputs[sidechainInputIndex];
            if (sc.data32 && sc.channel_count > 0 && sc.data32[0])
            {
                scL = sc.data32[0];
                scR = (sc.channel_count > 1 && sc.data32[1]) ? sc.data32[1] : scL;
            }
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

                engine->processControl(outq);
            }

            if (!engine->envFromSidechain)
                engine->processEnvFollower(inD[0][s], inD[1][s]);
            else if (scL)
                engine->processEnvFollower(scL[s], scR[s]);
            else
                engine->processEnvFollower(0.f, 0.f);

            engine->processAudio<routingMode, withFeedback, withNoise, withOS>(
                inD[0][s], inD[1][s], outD[0][s], outD[1][s]);

            blockPos = (blockPos + 1) & (blockSize - 1);
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

    void reset() noexcept override
    {
        // the host wants a clean start, so no filter or feedback tails carry over
        engine->reinitializeDSP();
        blockPos = 0;
    }

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
                SQLOG_AUDIO_DEBUG("Unknown inbound event of type " << nextEvent->type);
            }
            break;
            }
        }
        return true;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *ostream) noexcept override
    {
        // patchMain is authoritative. If no editor is open to keep it current, drain any
        // pending audio-thread updates into it first (we are the only consumer then).
        if (!engine->editorActive.load())
            engine->drainAudioToMainInto(engine->patchMain);

        return sst::plugininfra::patch_support::patchToOutStream(engine->patchMain, ostream);
    }

    bool stateLoad(const clap_istream *istream) noexcept override
    {
        // Load into a temp first so a parse failure never leaves patchMain half-written.
        auto tmp = std::make_unique<Patch>();
        if (!sst::plugininfra::patch_support::inStreamToPatch(istream, *tmp))
            return false;

        engine->patchMain.copyValuesFrom(*tmp);
        engine->uiForceRebuild++; // open editor rebuilds from patchMain

        if (isActive())
        {
            // Push the new patch to the audio-thread `patch`. This also rescans the host.
            Engine::sendEntirePatchToAudio(engine->patchMain, engine->mainToAudio, _host.host());
        }
        else if (_host.canUseParams())
        {
            // Not running: the next activate() copies patchMain into patch. Just tell the
            // host to re-read the loaded values (it reads them from patchMain).
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
        }
        return true;
    }

    bool implementsPresetLoad() const noexcept override { return true; }
    bool presetLoadFromLocation(uint32_t location_kind, const char *location,
                                const char *load_key) noexcept override
    {
        try
        {
            if (location_kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE)
            {
                auto p = fs::path(fs::u8path(location));
                if (p.extension() != PATCH_EXTENSION)
                {
                    SQLOG("Preset extension is not " PATCH_EXTENSION ": '" << location << "'");
                    return false;
                }

                std::ifstream t(p);
                if (!t.is_open())
                    return false;
                std::stringstream buffer;
                buffer << t.rdbuf();

                return loadPresetIntoPatchMain(buffer.str(),
                                               p.filename().replace_extension("").u8string());
            }

            if (location_kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && load_key)
            {
                auto pm = presets::PresetManager(nullptr);
                std::string cat, pat;
                if (!pm.fromFactoryLoadKey(load_key, cat, pat))
                {
                    SQLOG("Unknown factory preset '" << load_key << "'");
                    return false;
                }
                return loadPresetIntoPatchMain(pm.factoryPresetContents(cat, pat),
                                               presets::PresetManager::stripExtension(pat));
            }
        }
        catch (const fs::filesystem_error &e)
        {
            SQLOG("File System Error " << e.what() << " with " << location);
        }
        return false;
    }

    // same path as stateLoad, but a preset takes its name from the file and loads clean
    bool loadPresetIntoPatchMain(const std::string &xml, const std::string &name)
    {
        if (xml.empty())
            return false;

        auto tmp = std::make_unique<Patch>();
        if (!tmp->fromState(xml))
            return false;

        engine->patchMain.copyValuesFrom(*tmp);
        memset(engine->patchMain.name, 0, sizeof(engine->patchMain.name));
        strncpy(engine->patchMain.name, name.c_str(), sizeof(engine->patchMain.name) - 1);
        engine->patchMain.dirty = false;
        engine->uiForceRebuild++;

        if (isActive())
            Engine::sendEntirePatchToAudio(engine->patchMain, engine->mainToAudio, _host.host());
        else if (_host.canUseParams())
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    // All param reads come from patchMain (main-thread source of truth); never engine->patch.
    uint32_t paramsCount() const noexcept override { return engine->patchMain.params.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (!sst::plugininfra::patch_support::patchParamsInfo(paramIndex, info, engine->patchMain))
            return false;
        // patchParamsInfo cookies the patch it read, but the host hands the cookie back on
        // param events we resolve on the audio thread, so it must point into `patch`
        info->cookie = engine->clapCookieFor(info->id);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsValue(paramId, value, engine->patchMain);
    }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsValueToText(paramId, value, display,
                                                                       size, engine->patchMain);
    }
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsTextToValue(paramId, display, value,
                                                                       engine->patchMain);
    }
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        if (isActive())
        {
            // Audio thread: route param changes through the queue into `patch`.
            auto sz = in->size(in);
            for (uint32_t i = 0; i < sz; ++i)
                handleEvent(in->get(in, i));
            engine->snapAllParams();
            engine->processUIQueue(out);
        }
        else
        {
            // Main thread: patchMain is the truth; update it in place and echo out.
            engine->paramsFlushMainThread(in, out);
        }
    }

  public:
    bool implementsGui() const noexcept override { return clapJuceShim != nullptr; }
    std::unique_ptr<sst::clap_juce_shim::ClapJuceShim> clapJuceShim;
    ADD_SHIM_IMPLEMENTATION(clapJuceShim)
    ADD_SHIM_LINUX_TIMER(clapJuceShim)
    std::unique_ptr<juce::Component> createEditor() override
    {
        auto res = std::make_unique<baconpaul::twofilters::ui::PluginEditor>(
            engine->patchMain, engine->audioToMain, engine->mainToAudio, engine->editorActive,
            engine->uiForceRebuild, _host.host());

        res->onZoomChanged = [this](auto f)
        {
            if (_host.canUseGui() && clapJuceShim->isEditorAttached())
            {
                // SQLOG("onZoomChanged " << f);
                auto s = f * clapJuceShim->getGuiScale();
                guiSetSize(baconpaul::twofilters::edWidth * s, baconpaul::twofilters::edHeight * s);
                _host.guiRequestResize(baconpaul::twofilters::edWidth * s,
                                       baconpaul::twofilters::edHeight * s);
            }
        };

        onShow = [e = res.get()]()
        {
            // SQLOG("onShow with zoom factor " << e->zoomFactor);
            e->setZoomFactor(e->zoomFactor);
            return true;
        };
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

    bool registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg) override
    {
        if (!_host.canUsePosixFdSupport())
            return false;
        if (reg)
        {
            _host.posixFdSupportRegister(fd, flags);
        }
        else
        {
            _host.posixFdSupportUnregister(fd);
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

    struct RemotePage
    {
        std::string section, name;
        std::array<const Param *, CLAP_REMOTE_CONTROLS_COUNT> params{};
    };
    std::vector<RemotePage> remotePages;

    void buildRemotePages()
    {
        auto &pm = engine->patchMain;
        auto &f0 = pm.filterNodes[0];
        auto &f1 = pm.filterNodes[1];
        auto &rn = pm.routingNode;

        remotePages.push_back({"Filters",
                               "Filters",
                               {&f0.cutoff, &f0.resonance, &f0.morph, &f0.pan, &f1.cutoff,
                                &f1.resonance, &f1.morph, &f1.pan}});
        remotePages.push_back(
            {"Routing",
             "Routing",
             {&rn.mix, &rn.inputGain, &rn.outputGain, &rn.feedback, &rn.noiseLevel,
              &rn.filterBlendSerial, &rn.filterBlendParallel, nullptr}});

        // lfo pages mirror the two above, slot for slot
        for (int i = 0; i < numStepLFOs; ++i)
        {
            auto &l = pm.stepLfoNodes[i];
            auto sec = "Step LFO " + std::to_string(i + 1);
            auto pfx = "LFO " + std::to_string(i + 1) + " ";
            remotePages.push_back({sec,
                                   pfx + "Filters",
                                   {&l.toCO[0], &l.toRes[0], &l.toMorph[0], &l.toPan[0], &l.toCO[1],
                                    &l.toRes[1], &l.toMorph[1], &l.toPan[1]}});
            remotePages.push_back({sec,
                                   pfx + "Routing",
                                   {&l.toMix, &l.toPreG, &l.toPostG, &l.toFB, &l.toNoise,
                                    &l.toFiltBlend, &l.rate, &l.smooth}});
        }

        // follower controls on top, filter routes below; routing slots line up with the lfos
        auto &en = pm.envFollowerNode;
        remotePages.push_back({"Env Follower",
                               "Env Follower",
                               {&en.attack, &en.release, &en.gain, &en.source, &en.toCO[0],
                                &en.toRes[0], &en.toCO[1], &en.toRes[1]}});
        remotePages.push_back({"Env Follower",
                               "Env Routing",
                               {&en.toMix, &en.toPreG, nullptr, &en.toFB, &en.toNoise,
                                &en.toFiltBlend, &en.channel, nullptr}});
    }

    static uint32_t CLAP_ABI remoteControlsCount(const clap_plugin_t *plugin) noexcept
    {
        auto *self = static_cast<TwoFilters *>(plugin->plugin_data);
        return self->remotePages.size();
    }

    static bool CLAP_ABI remoteControlsGet(const clap_plugin_t *plugin, uint32_t pageIndex,
                                           clap_remote_controls_page_t *page) noexcept
    {
        auto *self = static_cast<TwoFilters *>(plugin->plugin_data);
        if (pageIndex >= self->remotePages.size())
            return false;

        auto &rp = self->remotePages[pageIndex];
        strncpy(page->section_name, rp.section.c_str(), CLAP_NAME_SIZE - 1);
        page->section_name[CLAP_NAME_SIZE - 1] = 0;
        strncpy(page->page_name, rp.name.c_str(), CLAP_NAME_SIZE - 1);
        page->page_name[CLAP_NAME_SIZE - 1] = 0;
        page->page_id = pageIndex + 1;
        for (int i = 0; i < CLAP_REMOTE_CONTROLS_COUNT; ++i)
            page->param_ids[i] = rp.params[i] ? rp.params[i]->meta.id : CLAP_INVALID_ID;
        page->is_for_preset = false;
        return true;
    }

    const void *extension(const char *id) noexcept override
    {
        if (strcmp(id, CLAP_PLUGIN_AS_VST3) == 0)
        {
            static clap_plugin_as_vst3 v3p{vst3_getNumMIDIChannels, vst3_supportedNoteExpressions};
            return &v3p;
        }

        if (strcmp(id, CLAP_EXT_REMOTE_CONTROLS) == 0 ||
            strcmp(id, CLAP_EXT_REMOTE_CONTROLS_COMPAT) == 0)
        {
            static clap_plugin_remote_controls rc{remoteControlsCount, remoteControlsGet};
            return &rc;
        }

        return nullptr;
    }
}; // namespace baconpaul::twofilters

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
