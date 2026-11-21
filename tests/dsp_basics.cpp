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

#include "catch2/catch2.hpp"

#include <cmath>
#include <algorithm>

#include "engine/steplfo_songpos.h"
#include "engine/engine.h"
#include "sst/basic-blocks/modulators/StepLFO.h"
#include "sst/basic-blocks/modulators/Transport.h"
#include "sst/basic-blocks/tables/EqualTuningProvider.h"
#include "sst/basic-blocks/dsp/RNG.h"

using namespace baconpaul::twofilters;

TEST_CASE("stepLFOSongPos anchors at the bar", "[lfo]")
{
    // EveryBar (group == 1), 4/4. On every downbeat the LFO is at step 0 / phase 0.
    for (double bar : {0.0, 4.0, 8.0, 12.0, 16.0, 100.0})
    {
        auto sp = stepLFOSongPos(bar, bar, 4.0, 1, 2.0f, 16);
        REQUIRE(sp.step == 0);
        REQUIRE(std::abs(sp.phase) < 1e-6f);
    }
}

TEST_CASE("stepLFOSongPos stays in range and tracks the rate", "[lfo]")
{
    for (double b = 0; b < 8.0; b += 0.005)
    {
        auto sp = stepLFOSongPos(b, 0.0, 4.0, 1, 2.0f, 16);
        REQUIRE(sp.step >= 0);
        REQUIRE(sp.step < 16);
        REQUIRE(sp.phase >= 0.0f);
        REQUIRE(sp.phase < 1.0f);
    }

    // rate 2 -> 2^(2-1) == 2 steps per beat
    REQUIRE(stepLFOSongPos(0.0, 0.0, 4.0, 1, 2.0f, 16).step == 0);
    REQUIRE(stepLFOSongPos(0.5, 0.0, 4.0, 1, 2.0f, 16).step == 1);
    REQUIRE(stepLFOSongPos(1.0, 0.0, 4.0, 1, 2.0f, 16).step == 2);
    auto sp = stepLFOSongPos(1.75, 0.0, 4.0, 1, 2.0f, 16); // 2 * 1.75 == 3.5
    REQUIRE(sp.step == 3);
    REQUIRE(std::abs(sp.phase - 0.5f) < 1e-5f);
}

TEST_CASE("stepLFOSongPos: a relocate to a bar is exact regardless of FP wobble", "[lfo]")
{
    // The old code used (int)beats % barLen and missed a relocate reported as 15.9999.
    // Song-position locking ties the reset to the host bar_start, so landing on bar 5
    // (beat 16) gives step 0 / phase ~0 whether the reported position is 16.0 or 16.0001.
    auto onBeat = stepLFOSongPos(16.0, 16.0, 4.0, 1, 3.0f, 16);
    auto justAfter = stepLFOSongPos(16.0001, 16.0, 4.0, 1, 3.0f, 16);
    REQUIRE(onBeat.step == 0);
    REQUIRE(justAfter.step == 0);
    REQUIRE(onBeat.phase < 1e-4f);
    REQUIRE(justAfter.phase < 1e-2f);
}

TEST_CASE("stepLFOSongPos multi-bar grouping", "[lfo]")
{
    // Every 2 bars (group == 2), 4/4: groups start at beats 0, 8, 16...
    REQUIRE(stepLFOSongPos(8.0, 8.0, 4.0, 2, 1.0f, 16).step == 0); // group boundary, resets

    // bar 4 (beat 12) is the 2nd bar of the group [bar3, bar4] - no reset there.
    // 1 step/beat, 4 beats into the group -> step 4.
    auto mid = stepLFOSongPos(12.0, 12.0, 4.0, 2, 1.0f, 16);
    REQUIRE(mid.step == 4);
}

TEST_CASE("stepLFOSongPos SongPos mode never resets", "[lfo]")
{
    // barsPerGroup < 1 == SongPos: position is a pure function of song beats, anchored at 0,
    // independent of the bar. 1 step/beat, 16 steps -> wraps every 16 beats, not every bar.
    REQUIRE(stepLFOSongPos(0.0, 0.0, 4.0, 0, 1.0f, 16).step == 0);
    REQUIRE(stepLFOSongPos(4.0, 4.0, 4.0, 0, 1.0f, 16).step == 4);   // bar 2 downbeat: NOT reset
    REQUIRE(stepLFOSongPos(8.0, 8.0, 4.0, 0, 1.0f, 16).step == 8);   // bar 3 downbeat: NOT reset
    REQUIRE(stepLFOSongPos(16.0, 16.0, 4.0, 0, 1.0f, 16).step == 0); // wrapped after 16 steps
    REQUIRE(stepLFOSongPos(20.0, 20.0, 4.0, 0, 1.0f, 16).step == 4);

    // Contrast: EveryBar (group 1) DOES reset at each of those downbeats.
    REQUIRE(stepLFOSongPos(4.0, 4.0, 4.0, 1, 1.0f, 16).step == 0);
    REQUIRE(stepLFOSongPos(8.0, 8.0, 4.0, 1, 1.0f, 16).step == 0);
}

TEST_CASE("stepLFOSongPos wraps modulo the step count", "[lfo]")
{
    // Long group so no bar reset in the window; 1 step/beat, 4 steps -> wraps every 4 beats.
    constexpr int rep = 4;
    REQUIRE(stepLFOSongPos(0.0, 0.0, 1000.0, 1, 1.0f, rep).step == 0);
    REQUIRE(stepLFOSongPos(1.0, 0.0, 1000.0, 1, 1.0f, rep).step == 1);
    REQUIRE(stepLFOSongPos(3.0, 0.0, 1000.0, 1, 1.0f, rep).step == 3);
    REQUIRE(stepLFOSongPos(4.0, 0.0, 1000.0, 1, 1.0f, rep).step == 0);
    REQUIRE(stepLFOSongPos(5.0, 0.0, 1000.0, 1, 1.0f, rep).step == 1);
}

TEST_CASE("stepLFOSongPos matches a free-running StepLFO", "[lfo]")
{
    // The strong test: the song-position math must reproduce exactly what the library's
    // own free-running increment produces, otherwise the locked phase would jump every
    // time the engine switches between the two paths.
    namespace mod = sst::basic_blocks::modulators;
    constexpr size_t bs = 8; // engine blockSize

    sst::basic_blocks::tables::EqualTuningProvider tp;
    tp.init();
    sst::basic_blocks::dsp::RNG rng;

    mod::StepLFO<bs> lfo(tp);
    mod::StepLFO<bs>::Storage storage;
    for (int i = 0; i < (int)storage.data.size(); ++i)
        storage.data[i] = std::sin(i * 0.7f); // arbitrary, non-constant steps
    storage.repeat = 16;
    storage.smooth = 0.f;
    storage.rateIsForSingleStep = true;

    mod::Transport transport;
    transport.tempo = 120.0;

    const double sr = 48000.0;
    const float rate = 4.0f; // 2^(4-1) == 8 steps/beat
    const int repeat = storage.repeat;

    // Sample rate must be set before assign() so the first phaseInc is computed correctly.
    lfo.setSampleRate(sr, 1.0 / sr);
    lfo.assign(&storage, rate, &transport, rng, true);
    lfo.retrigger();

    const double btIncr = (double)bs * transport.tempo / (60.0 * sr);
    double timeInBeats = 0.0;

    // SongPos mode (barsPerGroup 0) never injects a bar reset; compare directly to free-run.
    for (int blk = 0; blk < 8000; ++blk)
    {
        timeInBeats += btIncr; // engine advances the clock before positioning
        lfo.process(rate, 0, true, false, bs);

        auto sp = stepLFOSongPos(timeInBeats, 0.0, 4.0, 0 /* SongPos: no reset */, rate, repeat);

        double a = sp.step + sp.phase;
        double b = lfo.getCurrentStep() + lfo.phase;
        double d = std::abs(a - b);
        d = std::min(d, (double)repeat - d); // allow wrap-around at the cycle boundary
        INFO("blk " << blk << " songpos " << a << " freerun " << b);
        REQUIRE(d < 1e-3);
    }
}

namespace
{
bool envOutTryPush(const clap_output_events_t *, const clap_event_header_t *) { return true; }

void feedEnv(Engine &e, float L, float R, int n)
{
    for (int i = 0; i < n; ++i)
        e.processEnvFollower(L, R);
}
} // namespace

TEST_CASE("Envelope follower attack is one time constant and release spans the window",
          "[envfollower]")
{
    Engine engine;
    engine.setSampleRate(48000);
    auto &en = engine.patch.envFollowerNode;
    en.attack = std::log2(0.01f);
    en.release = std::log2(0.1f);
    engine.updateEnvFollower();

    feedEnv(engine, 1, 1, 480);
    REQUIRE(engine.envLevel == Approx(1 - std::exp(-1.f)).margin(1e-3));

    // from full depth, the release time is the time to fall through the whole db window
    feedEnv(engine, 1, 1, 48000);
    REQUIRE(Engine::envToMod(engine.envLevel, 0) == Approx(1.f).margin(1e-3));
    feedEnv(engine, 0, 0, 2400);
    REQUIRE(Engine::envToMod(engine.envLevel, 0) == Approx(0.5f).margin(0.01));
    feedEnv(engine, 0, 0, 2400);
    REQUIRE(Engine::envToMod(engine.envLevel, 0) == Approx(0.f).margin(0.01));
}

TEST_CASE("Envelope follower input is capped", "[envfollower]")
{
    Engine engine;
    engine.setSampleRate(48000);
    engine.patch.envFollowerNode.attack = -11;
    engine.updateEnvFollower();

    feedEnv(engine, 1e30f, 1e30f, 4800);
    REQUIRE(engine.envLevel <= Engine::followerInputCap);

    feedEnv(engine, std::nanf(""), 0.f, 10);
    REQUIRE(std::isfinite(engine.envLevel));
}

TEST_CASE("Envelope follower channel modes rectify the chosen signal", "[envfollower]")
{
    using EF = Patch::EnvFollowerNode;
    auto settle = [](int channel)
    {
        Engine engine;
        engine.setSampleRate(48000);
        engine.patch.envFollowerNode.attack = -11;
        engine.patch.envFollowerNode.channel = channel;
        engine.updateEnvFollower();
        feedEnv(engine, 0.8f, -0.4f, 4800);
        return engine.envLevel;
    };

    REQUIRE(settle(EF::LEFT) == Approx(0.8f).margin(1e-3));
    REQUIRE(settle(EF::RIGHT) == Approx(0.4f).margin(1e-3));
    REQUIRE(settle(EF::MID) == Approx(0.2f).margin(1e-3));
    REQUIRE(settle(EF::SIDE) == Approx(0.6f).margin(1e-3));
}

TEST_CASE("Envelope follower modulates the mix", "[envfollower]")
{
    // a nyquist square through the default lowpass is almost silent, so wet and dry differ
    auto run = [](float envInput)
    {
        Engine engine;
        engine.setSampleRate(48000);
        engine.patch.envFollowerNode.attack = -11;
        engine.patch.envFollowerNode.toMix = -1;
        clap_output_events_t out{nullptr, envOutTryPush};

        float oL{0}, oR{0}, in{0};
        for (int b = 0; b < 200; ++b)
        {
            engine.processControl(&out);
            for (int s = 0; s < blockSize; ++s)
            {
                engine.processEnvFollower(envInput, envInput);
                in = (s % 2) ? 0.3f : -0.3f;
                engine.processAudio<Engine::RoutingModes::Serial, false, false, false>(in, in, oL,
                                                                                       oR);
            }
        }
        return std::pair{in, oL};
    };

    auto [inQuiet, outQuiet] = run(0.f);
    REQUIRE(std::abs(outQuiet) < 0.1f);

    auto [inLoud, outLoud] = run(1.f);
    REQUIRE(outLoud == Approx(inLoud).margin(1e-4));
}

TEST_CASE("Envelope follower maps a db window to modulation", "[envfollower]")
{
    auto fl = Patch::EnvFollowerNode::floorDb;
    REQUIRE(Engine::envToMod(1.f, 0) == Approx(1.f));
    REQUIRE(Engine::envToMod(2.f, 0) == Approx(1.f));
    REQUIRE(Engine::envToMod(0.f, 0) == Approx(0.f));
    // fast log is within 0.05db, about 0.001 of depth
    auto dm = 0.002;
    REQUIRE(Engine::envToMod(std::pow(10.f, fl / 40), 0) == Approx(0.5f).margin(dm));

    // gain slides the window, so a quiet source can reach full depth
    REQUIRE(Engine::envToMod(std::pow(10.f, -12.f / 20), 0) == Approx(0.75f).margin(dm));
    REQUIRE(Engine::envToMod(std::pow(10.f, -12.f / 20), 12) == Approx(1.f).margin(dm));
    REQUIRE(Engine::envToMod(1.f, -24) == Approx(0.5f).margin(dm));
}

TEST_CASE("Fast log2 tracks log2", "[envfollower]")
{
    for (float x = 1e-6f; x < 100.f; x *= 1.013f)
    {
        INFO("x=" << x);
        REQUIRE(Engine::fastLog2(x) == Approx(std::log2(x)).margin(0.008));
    }
    REQUIRE(Engine::fastLog2(1.f) == Approx(0.f).margin(1e-6));
    REQUIRE(Engine::fastLog2(0.25f) == Approx(-2.f).margin(1e-6));
}

TEST_CASE("Reactivating clears the oversampling filters", "[recovery]")
{
    Engine e;
    e.patch.routingNode.oversample = 1;
    e.setSampleRate(48000);
    clap_output_events_t out{nullptr, envOutTryPush};
    e.processControl(&out);
    REQUIRE(e.overSampling);

    float nan = std::nanf(""), u[2], v[2], d[2]{nan, nan}, o, p;
    e.hrUp.process_sample_U2(nan, nan, u, v);
    e.hrDn.process_sample_D2(d, d, o, p);

    e.setSampleRate(48000);
    bool ok{true};
    for (int b = 0; b < 100; ++b)
    {
        e.processControl(&out);
        for (int s = 0; s < blockSize; ++s)
        {
            float L, R;
            e.processAudio<Engine::RoutingModes::Serial, false, false, true>(0.1f, 0.1f, L, R);
            ok = ok && std::isfinite(L) && std::isfinite(R);
        }
    }
    REQUIRE(ok);
}

TEST_CASE("Reinitializing clears a poisoned filter and follower", "[recovery]")
{
    Engine e;
    e.patch.routingNode.feedbackPower = 1;
    e.patch.routingNode.feedback = 0.5;
    e.setSampleRate(48000);
    clap_output_events_t out{nullptr, envOutTryPush};

    auto run = [&](float bad)
    {
        bool ok{true};
        for (int b = 0; b < 100; ++b)
        {
            e.processControl(&out);
            for (int s = 0; s < blockSize; ++s)
            {
                auto x = (b == 0 && s == 3) ? bad : 0.1f;
                float L, R;
                e.processEnvFollower(x, x);
                e.processAudio<Engine::RoutingModes::Serial, true, false, false>(x, x, L, R);
                ok = ok && std::isfinite(L) && std::isfinite(R);
            }
        }
        return ok && std::isfinite(e.envLevel);
    };

    REQUIRE(!run(std::nanf("")));
    e.reinitializeDSP();
    REQUIRE(run(0.1f));
}

TEST_CASE("Reactivating clears the envelope follower", "[recovery]")
{
    Engine e;
    e.setSampleRate(48000);
    // the input cap stops a nan getting in, so poison the state directly
    e.envFollower.zL = std::nanf("");
    feedEnv(e, 0.f, 0.f, 1);
    REQUIRE(!std::isfinite(e.envLevel));
    e.setSampleRate(48000);
    feedEnv(e, 0.1f, 0.1f, 480);
    REQUIRE(std::isfinite(e.envLevel));
}

TEST_CASE("Reinitializing the follower leaves the filters running", "[recovery]")
{
    // two engines are too big for the stack together
    auto ap = std::make_unique<Engine>(), bp = std::make_unique<Engine>();
    auto &a = *ap, &b = *bp;
    a.setSampleRate(48000);
    b.setSampleRate(48000);
    clap_output_events_t out{nullptr, envOutTryPush};

    auto block = [&](Engine &e, float x, float &L, float &R)
    {
        e.processControl(&out);
        for (int s = 0; s < blockSize; ++s)
        {
            e.processEnvFollower(x, x);
            e.processAudio<Engine::RoutingModes::Serial, false, false, false>(x, x, L, R);
        }
    };

    float aL, aR, bL, bR;
    for (int i = 0; i < 50; ++i)
    {
        block(a, 0.3f, aL, aR);
        block(b, 0.3f, bL, bR);
    }

    b.envFollower.zL = std::nanf("");
    feedEnv(b, 0.f, 0.f, 1);
    REQUIRE(!std::isfinite(b.envLevel));
    b.reinitializeFollower();
    REQUIRE(b.envLevel == 0.f);

    for (int i = 0; i < 50; ++i)
    {
        block(a, 0.f, aL, aR);
        block(b, 0.f, bL, bR);
        REQUIRE(aL == bL);
        REQUIRE(aR == bR);
    }
}

TEST_CASE("A zero time signature behaves as 4/4", "[robustness]")
{
    using TR = sst::basic_blocks::modulators::Transport;
    Engine e;
    e.patch.routingNode.retriggerMode = (int)Engine::RetrigModes::EveryBar;
    e.patch.stepLfoNodes[0].toCO[0] = 12;
    e.setSampleRate(48000);
    e.transport.status = TR::PLAYING;
    clap_output_events_t out{nullptr, envOutTryPush};

    for (auto [num, den] : {std::pair{0, 0}, std::pair{0, 4}, std::pair{4, 0}})
    {
        INFO("signature " << num << "/" << den);
        e.transport.signature.numerator = num;
        e.transport.signature.denominator = den;
        bool ok{true};
        for (int b = 0; b < 200; ++b)
        {
            e.processControl(&out);
            ok = ok && std::isfinite(e.lfos[0].output);
            for (int s = 0; s < blockSize; ++s)
            {
                float L, R;
                e.processAudio<Engine::RoutingModes::Serial, false, false, false>(0.1f, 0.1f, L, R);
                ok = ok && std::isfinite(L) && std::isfinite(R);
            }
        }
        REQUIRE(ok);
    }
}

TEST_CASE("Modulation past nyquist stays finite", "[robustness]")
{
    // vember classic went to nan within samples once modulation pushed cutoff past nyquist
    Engine e;
    auto &fn = e.patch.filterNodes[0];
    fn.model = sst::filtersplusplus::FilterModel::VemberClassic;
    fn.config = sst::filtersplusplus::Filter::availableModelConfigurations(fn.model, true)[0];
    fn.cutoff = 70;
    e.patch.envFollowerNode.toCO[0] = 36;
    e.patch.envFollowerNode.attack = -11;
    e.setSampleRate(48000);
    clap_output_events_t out{nullptr, envOutTryPush};

    bool ok{true};
    for (int b = 0; b < 600; ++b)
    {
        e.processControl(&out);
        for (int s = 0; s < blockSize; ++s)
        {
            auto x = (s & 1) ? 0.9f : -0.9f;
            float L, R;
            e.processEnvFollower(x, x);
            e.processAudio<Engine::RoutingModes::Serial, false, false, false>(x, x, L, R);
            ok = ok && std::isfinite(L) && std::isfinite(R);
        }
    }
    REQUIRE(ok);
}

TEST_CASE("Loading a damaged state is sanitized", "[robustness]")
{
    auto src = std::make_unique<Patch>();
    src->routingNode.routingMode = 7;
    src->stepLfoNodes[0].stepCount = 0;
    src->filterNodes[0].cutoff = std::nanf("");
    src->filterNodes[1].model = (sst::filtersplusplus::FilterModel)9999;
    auto state = src->toState();

    auto dst = std::make_unique<Patch>();
    REQUIRE(dst->fromState(state));
    REQUIRE(dst->routingNode.routingMode.value <= dst->routingNode.routingMode.meta.maxVal);
    REQUIRE(dst->stepLfoNodes[0].stepCount.value >= dst->stepLfoNodes[0].stepCount.meta.minVal);
    REQUIRE(std::isfinite(dst->filterNodes[0].cutoff.value));
    REQUIRE(dst->filterNodes[1].model == sst::filtersplusplus::FilterModel::None);
}

TEST_CASE("Host values are clamped to the param range", "[robustness]")
{
    Engine e;
    e.setSampleRate(48000);
    auto &rm = e.patch.routingNode.routingMode;
    e.handleParamValue(&rm, rm.meta.id, 99.f);
    REQUIRE(rm.value == rm.meta.maxVal);
}
