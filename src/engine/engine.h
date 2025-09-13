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
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "sst/basic-blocks/tables/EqualTuningProvider.h"
#include "sst/basic-blocks/modulators/StepLFO.h"
#include "sst/cpputils/ring_buffer.h"

#include "filesystem/import.h"

#include "configuration.h"

#include "engine/patch.h"

#include "sst/basic-blocks/dsp/LagCollection.h"
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/BlockInterpolators.h"
#include "sst/filters++.h"

namespace baconpaul::twofilters
{
struct Engine
{
    Patch patch;

    enum struct RoutingModes
    {
        Serial = 0,
        Parallel_FBBoth = 1,
        Parallel_FBOne = 2,
        Parallel_FBEach = 3
    };

    enum struct RetrigModes
    {
        EveryBar = 0,
        Every2Bars = 1,
        Every4Bars = 2,
        OnTransport = 3
    };

    Engine();
    ~Engine();

    std::array<sst::filtersplusplus::Filter, numFilters> filters;
    bool useFeedback{false};
    float fbL{0}, fbR{0}, fb2L{0}, fb2R{0};

    sst::basic_blocks::dsp::RNG rng;
    using stepLfo_t = sst::basic_blocks::modulators::StepLFO<blockSize>;
    static_assert(maxSteps <= stepLfo_t::Storage::stepLfoSteps);
    std::array<stepLfo_t, numStepLFOs> lfos;
    std::array<stepLfo_t::Storage, numStepLFOs> lfoStorage;
    sst::basic_blocks::tables::EqualTuningProvider tuningProvider;
    sst::basic_blocks::modulators::Transport transport;
    uint32_t lastStatus{sst::basic_blocks::modulators::Transport::STOPPED};
    void sendUpdateLfo();
    void reassignLfos();
    void updateLfoStorage();
    static void updateLfoStorageFromTo(const Patch &p, int node, stepLfo_t::Storage &to);

    void restartLfos();

    bool audioRunning{true};
    int beginEndParamGestureCount{0};

    double sampleRate{1}, sampleRateInv{1};
    void setSampleRate(double sampleRate);

    bool didResetInLargerBlock{false};
    void beginLargerBlock() { didResetInLargerBlock = false; }
    void processControl(const clap_output_events_t *);

    float noiseState[2][2]{0, 0};
    sst::basic_blocks::dsp::lipol<float, blockSize, true> blendLipol1, blendLipol2;
    sst::basic_blocks::dsp::pan_laws::panmatrix_t panMatrix[2];
    sst::basic_blocks::dsp::OnePoleLag<float, true> panLag[2];

    void applyPan(float &L, float &R, int which)
    {
        auto tL = (panMatrix[which][0] * L + panMatrix[which][2] * R);
        auto tR = (panMatrix[which][1] * R + panMatrix[which][3] * L);
        L = tL;
        R = tR;
    }

    template <RoutingModes mode, bool fb, bool withNoise>
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

        float inG = patch.routingNode.inputGain + lfos[0].output * patch.stepLfoNodes[0].toPreG +
                    lfos[1].output * patch.stepLfoNodes[1].toPreG;
        inG = std::clamp(inG, 0.f, 1.f);
        inG = inG * inG * inG;
        inL *= inG;
        inR *= inG;

        if constexpr (withNoise)
        {
            float nsG = std::clamp(patch.routingNode.noiseLevel +
                                       lfos[0].output * patch.stepLfoNodes[0].toNoise +
                                       lfos[1].output * patch.stepLfoNodes[1].toNoise,
                                   0.f, 1.f);
            nsG = nsG * nsG * nsG;
            auto n1 = sst::basic_blocks::dsp::correlated_noise_o2mk2_supplied_value(
                noiseState[0][0], noiseState[0][1], 0, rng.unifPM1());
            auto n2 = sst::basic_blocks::dsp::correlated_noise_o2mk2_supplied_value(
                noiseState[1][0], noiseState[1][1], 0, rng.unifPM1());
            inL += nsG * n1;
            inR += nsG * n2;
        }

        if constexpr (mode == RoutingModes::Serial)
        {
            if constexpr (fb)
            {
                inL += fbL;
                inR += fbR;
            }

            float out1L, out1R, out2L, out2R;
            filters[0].processStereoSample(inL, inR, out1L, out1R);
            applyPan(out1L, out1R, 0);

            filters[1].processStereoSample(out1L, out1R, out2L, out2R);
            applyPan(out2L, out2R, 1);

            outL = blendLipol1.v * out1L + blendLipol2.v * out2L;
            outR = blendLipol1.v * out1R + blendLipol2.v * out2R;

            if constexpr (fb)
            {
                float fblev = patch.routingNode.feedback;
                fblev += lfos[0].output * patch.stepLfoNodes[0].toFB +
                         lfos[1].output * patch.stepLfoNodes[1].toFB;
                fblev = std::clamp(fblev, 0.f, 1.f);
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

            applyPan(t0L, t0R, 0);
            applyPan(t1L, t1R, 1);
            outL = blendLipol1.v * t0L + blendLipol2.v * t1L;
            outR = blendLipol1.v * t0R + blendLipol2.v * t1R;

            // SQLOG(SQD(blendLipol1.v) << SQD(blendLipol2.v));
            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev += lfos[0].output * patch.stepLfoNodes[0].toFB +
                         lfos[1].output * patch.stepLfoNodes[1].toFB;
                fblev = std::clamp(fblev, 0.f, 1.f);

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

            applyPan(t0L, t0R, 0);
            applyPan(t1L, t1R, 1);
            outL = blendLipol1.v * t0L + blendLipol2.v * t1L;
            outR = blendLipol1.v * t0R + blendLipol2.v * t1R;

            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev += lfos[0].output * patch.stepLfoNodes[0].toFB +
                         lfos[1].output * patch.stepLfoNodes[1].toFB;
                fblev = std::clamp(fblev, 0.f, 1.f);

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

            applyPan(t0L, t0R, 0);
            applyPan(t1L, t1R, 1);
            outL = blendLipol1.v * t0L + blendLipol2.v * t1L;
            outR = blendLipol1.v * t0R + blendLipol2.v * t1R;

            if constexpr (fb)
            {
                // Only need to run 1 if we have feedback
                float fblev = patch.routingNode.feedback;
                fblev += lfos[0].output * patch.stepLfoNodes[0].toFB +
                         lfos[1].output * patch.stepLfoNodes[1].toFB;
                fblev = std::clamp(fblev, 0.f, 1.f);

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
        mx += lfos[0].output * patch.stepLfoNodes[0].toMix +
              lfos[1].output * patch.stepLfoNodes[1].toMix;
        mx = std::clamp(mx, 0.f, 1.f);

        float outG = patch.routingNode.outputGain + lfos[0].output * patch.stepLfoNodes[0].toPostG +
                     lfos[1].output * patch.stepLfoNodes[1].toPostG;
        outG = std::clamp(outG, 0.f, 1.f);
        outG = outG * outG * outG;
        outL *= outG;
        outR *= outG;

        outL = mx * outL + (1.0 - mx) * origL;
        outR = mx * outR + (1.0 - mx) * origR;

        outL = std::clamp(outL, -2.5f, 2.5f);
        outR = std::clamp(outR, -2.5f, 2.5f);

        blendLipol1.process();
        blendLipol2.process();

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
            UPDATE_LFOSTEP,
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

        reassignLfos();
    }

    bool activeFilter[2]{true, true};
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
