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
