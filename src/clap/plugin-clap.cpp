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

#include <memory>
#include "sst/plugininfra/patch-support/patch_base_clap_adapter.h"
#include "sst/plugininfra/cpufeatures.h"

#include "sst/basic-blocks/modulators/TransportClapAdapter.h"

#include "neui-ui/neui-editor.h"

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

struct TwoFilters : public plugHelper_t
{
    TwoFilters(const clap_host *h) : plugHelper_t(getDescriptor(), h)
    {
        engine = std::make_unique<Engine>();

        engine->clapHost = h;
    }
    virtual ~TwoFilters() {}

    std::unique_ptr<Engine> engine;
    size_t blockPos{0};

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        // The audio thread is stopped here; seed it from the main-thread source of truth.
        engine->patch.copyValuesFrom(engine->patchMain);
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

    bool implementsParams() const noexcept override { return true; }
    // All param reads come from patchMain (main-thread source of truth); never engine->patch.
    uint32_t paramsCount() const noexcept override { return engine->patchMain.params.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return sst::plugininfra::patch_support::patchParamsInfo(paramIndex, info,
                                                                engine->patchMain);
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
    /*
     * The gui extension, straight onto the neui editor - no shim. The neui
     * embed contract wants set_parent before show, which is exactly the
     * clap gui call order.
     */
    std::unique_ptr<ui::NeuiEditor> editor;

    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override
    {
        if (isFloating)
            return false;
#if defined(__APPLE__)
        return strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
        return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
        return strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
    }
    bool guiGetPreferredApi(const char **api, bool *isFloating) noexcept override
    {
#if defined(__APPLE__)
        *api = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
        *api = CLAP_WINDOW_API_WIN32;
#else
        *api = CLAP_WINDOW_API_X11;
#endif
        *isFloating = false;
        return true;
    }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        editor = std::make_unique<ui::NeuiEditor>();
        if (!editor->valid())
        {
            editor.reset();
            return false;
        }
        return true;
    }
    void guiDestroy() noexcept override
    {
#if defined(__linux__)
        if (editor && guiTimerId != CLAP_INVALID_ID && _host.canUseTimerSupport())
        {
            _host.timerSupportUnregister(guiTimerId);
            guiTimerId = CLAP_INVALID_ID;
        }
        if (editor && guiPosixFd >= 0 && _host.canUsePosixFdSupport())
        {
            _host.posixFdSupportUnregister(guiPosixFd);
            guiPosixFd = -1;
        }
#endif
        editor.reset();
    }
    bool guiSetScale(double scale) noexcept override
    {
        guiScale = scale;
        return true;
    }
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (!editor)
            return false;
        *width = editor->width();
        *height = editor->height();
        return true;
    }
    bool guiCanResize() const noexcept override { return false; }
    bool guiSetParent(const clap_window *window) noexcept override
    {
        if (!editor)
            return false;
#if defined(__APPLE__)
        auto res = editor->setParent(window->cocoa);
#elif defined(_WIN32)
        auto res = editor->setParent(window->win32);
#else
        auto res = editor->setParent(reinterpret_cast<void *>(uintptr_t(window->x11)));
#endif
        if (!res)
            return false;

#if defined(__linux__)
        // neui owns no loop embedded; drive it off the host timer and the
        // frame's X connection fd.
        if (_host.canUseTimerSupport())
            _host.timerSupportRegister(16, &guiTimerId);
        auto fd = editor->eventFd();
        if (fd >= 0 && _host.canUsePosixFdSupport())
        {
            _host.posixFdSupportRegister(fd, CLAP_POSIX_FD_READ);
            guiPosixFd = fd;
        }
#endif
        return true;
    }
    bool guiShow() noexcept override
    {
        if (!editor)
            return false;
        editor->show();
        return true;
    }
    bool guiHide() noexcept override
    {
        if (!editor)
            return false;
        editor->hide();
        return true;
    }

    double guiScale{1.0};
#if defined(__linux__)
    clap_id guiTimerId{CLAP_INVALID_ID};
    int guiPosixFd{-1};

    bool implementsTimerSupport() const noexcept override { return true; }
    void onTimer(clap_id timerId) noexcept override
    {
        if (editor && timerId == guiTimerId)
            editor->pumpAndTick();
    }
    bool implementsPosixFdSupport() const noexcept override { return true; }
    void onPosixFd(int fd, clap_posix_fd_flags_t flags) noexcept override
    {
        if (editor && fd == guiPosixFd)
            editor->pumpAndTick();
    }
#endif

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
