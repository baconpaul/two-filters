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
#include "sst/basic-blocks/dsp/RNG.h"
#include "sst/basic-blocks/tables/EqualTuningProvider.h"
#include "sst/basic-blocks/modulators/StepLFO.h"
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

    enum struct RoutingModes
    {
        Serial_Post2 = 0,
        Serial_Post1 = 1,
        Parallel_FBBoth = 2,
        Parallel_FBOne = 3,
        Parallel_FBEach = 4
    };

    Engine();
    ~Engine();

    std::array<sst::filtersplusplus::Filter, numFilters> filters;
    bool useFeedback{false};
    float fbL{0}, fbR{0}, fb2L{0}, fb2R{0};

    sst::basic_blocks::dsp::RNG rng;
    using stepLfo_t = sst::basic_blocks::modulators::StepLFO<blockSize>;
    static_assert(maxSteps == stepLfo_t::Storage::stepLfoSteps);
    std::array<stepLfo_t, numStepLFOs> lfos;
    std::array<stepLfo_t::Storage, numStepLFOs> lfoStorage;
    sst::basic_blocks::tables::EqualTuningProvider tuningProvider;

    bool audioRunning{true};
    int beginEndParamGestureCount{0};

    double sampleRate{1}, sampleRateInv{1};
    void setSampleRate(double sampleRate);

    void processControl(const clap_output_events_t *);

    template <RoutingModes mode, bool fb>
    void processAudio(float inL, float inR, float &outL, float &outR)
    {
        if (!audioRunning)
        {
            outL = 0;
            outR = 0;
            return;
        }

        auto origL = inL;
        auto origR = inR;

        if constexpr (mode == RoutingModes::Serial_Post2)
        {
            if constexpr (fb)
            {
                inL += fbL;
                inR += fbR;
            }

            filters[0].processStereoSample(inL, inR, outL, outR);
            filters[1].processStereoSample(outL, outR, outL, outR);

            if constexpr (fb)
            {
                float fblev = patch.routingNode.feedback;
                fblev = fblev * fblev * fblev;

                // y = x * ( 27 + x * x ) / ( 27 + 9 * x * x );
                auto sat = [](float x)
                {
                    x = std::clamp(x, -2.f, 2.f);
                    return x * (27 + x * x) / (27 + 9 * x * x);
                };
                fbL = sat(fblev * outL);
                fbR = sat(fblev * outR);
            }
        }
        else if constexpr (mode == RoutingModes::Serial_Post1)
        {
            if constexpr (fb)
            {
                inL += fbL;
                inR += fbR;
            }

            filters[0].processStereoSample(inL, inR, outL, outR);

            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float tmpL, tmpR;
                filters[1].processStereoSample(outL, outR, tmpL, tmpR);

                float fblev = patch.routingNode.feedback;
                fblev = fblev * fblev * fblev;

                // y = x * ( 27 + x * x ) / ( 27 + 9 * x * x );
                auto sat = [](float x) { return x * (27 + x * x) / (27 + 9 * x * x); };
                fbL = sat(fblev * tmpL);
                fbR = sat(fblev * tmpR);
            }
        }
        else if constexpr (mode == RoutingModes::Parallel_FBBoth)
        {
            if constexpr (fb)
            {
                inL += fbL;
                inR += fbR;
            }

            float t0L, t0R, t1L, t1R;
            filters[0].processStereoSample(inL, inR, t0L, t0R);
            filters[1].processStereoSample(inL, inR, t1L, t1R);

            outL = t0L + t1L;
            outR = t0R + t1R;
            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev = fblev * fblev * fblev;

                // y = x * ( 27 + x * x ) / ( 27 + 9 * x * x );
                auto sat = [](float x) { return x * (27 + x * x) / (27 + 9 * x * x); };
                fbL = sat(fblev * outL);
                fbR = sat(fblev * outR);
            }
        }
        else if constexpr (mode == RoutingModes::Parallel_FBOne)
        {
            float t0L, t0R, t1L, t1R;
            filters[1].processStereoSample(inL, inR, t1L, t1R);

            if constexpr (fb)
            {
                inL += fbL;
                inR += fbR;
            }
            filters[0].processStereoSample(inL, inR, t0L, t0R);

            outL = t0L + t1L;
            outR = t0R + t1R;
            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev = fblev * fblev * fblev;

                // y = x * ( 27 + x * x ) / ( 27 + 9 * x * x );
                auto sat = [](float x) { return x * (27 + x * x) / (27 + 9 * x * x); };
                fbL = sat(fblev * t0L);
                fbR = sat(fblev * t0R);
            }
        }
        else if constexpr (mode == RoutingModes::Parallel_FBEach)
        {
            float t0L, t0R, t1L, t1R;
            float i1L{inL}, i1R{inR}, i2L{inL}, i2R{inR};

            if constexpr (fb)
            {
                i1L += fbL;
                i1R += fbR;
            }
            filters[0].processStereoSample(i1L, i1R, t0L, t0R);

            if constexpr (fb)
            {
                i2L += fb2L;
                i2R += fb2R;
            }
            filters[1].processStereoSample(i2L, i2R, t1L, t1R);

            outL = t0L + t1L;
            outR = t0R + t1R;
            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev = fblev * fblev * fblev;

                // y = x * ( 27 + x * x ) / ( 27 + 9 * x * x );
                auto sat = [](float x) { return x * (27 + x * x) / (27 + 9 * x * x); };
                fbL = sat(fblev * t0L);
                fbR = sat(fblev * t0R);
                fb2L = sat(fblev * t1L);
                fb2R = sat(fblev * t1R);
            }
        }

        float mx = patch.routingNode.mix;
        outL = mx * outL + (1 - mx) * origL;
        outR = mx * outR + (1 - mx) * origR;

        outL = std::clamp(outL, -1.5f, 1.5f);
        outR = std::clamp(outR, -1.5f, 1.5f);

        if (isEditorAttached)
        {
            vuPeak.process(outL, outR);
        }
    }

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

    float combDelays[2][4][sst::filters::utilities::MAX_FB_COMB +
                           sst::filters::utilities::SincTable::FIRipol_N];

    const clap_host_t *clapHost{nullptr};
};
} // namespace baconpaul::twofilters
#endif // SYNTH_H
