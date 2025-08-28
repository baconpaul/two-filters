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

#ifndef BACONPAUL_TWOFILTERS_ENGINE_ENGINE_H
#define BACONPAUL_TWOFILTERS_ENGINE_ENGINE_H

#include <memory>
#include <array>

#include "sst/basic-blocks/dsp/LanczosResampler.h"

#include <clap/clap.h>
#include "sst/basic-blocks/dsp/Lag.h"
#include "sst/basic-blocks/dsp/VUPeak.h"
#include "sst/basic-blocks/tables/EqualTuningProvider.h"
#include "sst/cpputils/ring_buffer.h"

#include "filesystem/import.h"

#include "configuration.h"

#include "engine/patch.h"

#include "sst/basic-blocks/dsp/LagCollection.h"
#include "sst/filters++.h"

namespace baconpaul::twofilters
{
struct Engine
{
    Patch patch;

    Engine();
    ~Engine();

    std::array<sst::filtersplusplus::Filter, numFilters> filters;

    bool audioRunning{true};
    int beginEndParamGestureCount{0};

    double sampleRate{1}, sampleRateInv{1};
    void setSampleRate(double sampleRate);

    void processControl(const clap_output_events_t *);
    void processAudio(const float inL, const float inR, float &outL, float &outR);
    void processUIQueue(const clap_output_events_t *);

    void handleParamValue(Param *p, uint32_t pid, float value);

    // UI Communication
    struct AudioToUIMsg
    {
        enum Action : uint32_t
        {
            UPDATE_PARAM,
            UPDATE_VU,
            SET_PATCH_NAME,
            SET_PATCH_DIRTY_STATE,
            DO_PARAM_RESCAN,
            SEND_SAMPLE_RATE,
            SEND_FILTER_CONFIG,
        } action;
        uint32_t paramId{0};
        float value{0}, value2{0};
        const char *patchNamePointer{0};
        uint32_t uintValues[5]{0, 0, 0, 0, 0};
    };
    struct MainToAudioMsg
    {
        enum Action : uint32_t
        {
            REQUEST_REFRESH,
            SET_PARAM,
            SET_PARAM_WITHOUT_NOTIFYING,
            BEGIN_EDIT,
            END_EDIT,
            SET_FILTER_MODEL,
            STOP_AUDIO,
            START_AUDIO,
            SEND_PATCH_NAME,
            SEND_PATCH_IS_CLEAN,
            SEND_POST_LOAD,
            SEND_REQUEST_RESCAN,
            EDITOR_ATTACH_DETATCH, // paramid is true for attach and false for detach
            SEND_PREP_FOR_STREAM,
        } action;
        uint32_t paramId{0};
        float value{0};
        const char *uiManagedPointer{nullptr};
        uint32_t uintValues[5]{0, 0, 0, 0, 0};
    };
    using audioToUIQueue_t = sst::cpputils::SimpleRingBuffer<AudioToUIMsg, 1024 * 16>;
    using mainToAudioQueue_T = sst::cpputils::SimpleRingBuffer<MainToAudioMsg, 1024 * 64>;
    audioToUIQueue_t audioToUi;
    mainToAudioQueue_T mainToAudio;
    std::atomic<bool> doFullRefresh{false};
    bool isEditorAttached{false};
    sst::basic_blocks::dsp::UIComponentLagHandler lagHandler;

    std::atomic<bool> readyForStream{false};
    void prepForStream()
    {
        SQLOG("Ready for Stream");
        if (lagHandler.active)
            lagHandler.instantlySnap();

        for (auto &p : paramLagSet)
        {
            p.lag.snapToTarget();
            p.value = p.lag.v;
        }
        paramLagSet.removeAll();

        patch.dirty = false;
        doFullRefresh = true;
        readyForStream = true;
    }

    void pushFullUIRefresh();
    void postLoad()
    {
        doFullRefresh = true;

        for (auto &[i, p] : patch.paramMap)
        {
            p->lag.snapTo(p->value);
        }
    }

    void setupFilter(int instance);

    std::atomic<bool> onMainRescanParams{false};
    void onMainThread();

    sst::cpputils::active_set_overlay<Param> paramLagSet;

    sst::basic_blocks::dsp::VUPeak vuPeak;
    int32_t updateVuEvery{(int32_t)(48000 * 2.5 / 60 / blockSize)}; // approx
    int32_t lastVuUpdate{updateVuEvery};

    const clap_host_t *clapHost{nullptr};
};
} // namespace baconpaul::twofilters
#endif // SYNTH_H
