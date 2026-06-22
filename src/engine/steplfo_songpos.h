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

#ifndef BACONPAUL_TWOFILTERS_ENGINE_STEPLFO_SONGPOS_H
#define BACONPAUL_TWOFILTERS_ENGINE_STEPLFO_SONGPOS_H

#include <cmath>

namespace baconpaul::twofilters
{
/*
 * Song-position-locked positioning for the tempo-synced step LFO.
 *
 * Rather than detecting bar boundaries and calling retrigger() (phase 0), we derive the
 * LFO's (step, phase) directly from the host song position. This makes restart-on-bar-move
 * exact and makes scrub / loop / relocate deterministic - the LFO is a pure function of
 * where the transport says we are.
 *
 * Rate -> steps:
 *   StepLFO::UpdatePhaseIncrement (tempoSync, rateIsForSingleStep == true, which is what the
 *   engine sets) advances phase by
 *       phaseInc/block = blockSize * 2^rate * srInv * tempo/120
 *   and one whole step is phase == 1. Converting to a per-beat rate (beatsPerSec = tempo/60)
 *   the blockSize/sr terms cancel and you get
 *       stepsPerBeat = 2^rate * (60/120) = 2^(rate-1).
 *   This is the same relationship six-sines uses in node_support.h (just expressed in beats
 *   rather than seconds), so the song-locked phase matches the free-running increment.
 *
 * Anchor:
 *   barsPerGroup < 1 is the "SongPos" mode: never reset, anchor at beat 0, so the position
 *   is a pure function of the song beats (the sequence simply repeats every repeat/stepsPerBeat
 *   beats from song start).
 *   barsPerGroup >= 1 anchors the retrigger interval (1 / 2 / 4 bars) to the host bar grid
 *   using bar_start (transport.lastBarStartInBeats). gridOffset is where that grid sits
 *   relative to beat 0 (zero for a normal song that starts on a downbeat), and the current
 *   group anchor is the most recent multiple of the group length on that grid. Exact for
 *   "Each Bar"; for the multi-bar modes the group is aligned to the bar grid and counted from
 *   the grid origin (sample-accurate bar-1-of-group alignment would need bar_number, which the
 *   transport adapter does not currently carry).
 */
struct StepLFOSongPos
{
    int step{0};
    float phase{0.f};
};

inline StepLFOSongPos stepLFOSongPos(double timeInBeats, double barStartBeats, double beatsPerBar,
                                     int barsPerGroup, float rate, int repeat)
{
    if (repeat < 1)
        repeat = 1;
    if (beatsPerBar <= 0.0)
        beatsPerBar = 4.0;

    // barsPerGroup < 1 == SongPos: never reset, anchor at beat 0. Otherwise anchor to the
    // most recent N-bar group on the host bar grid.
    double anchor = 0.0;
    if (barsPerGroup >= 1)
    {
        double groupLen = (double)barsPerGroup * beatsPerBar;
        double gridOffset =
            barStartBeats - std::floor(barStartBeats / beatsPerBar + 0.5) * beatsPerBar;
        anchor = std::floor((timeInBeats - gridOffset) / groupLen) * groupLen + gridOffset;
    }

    double beatsSinceAnchor = timeInBeats - anchor;
    if (beatsSinceAnchor < 0.0)
        beatsSinceAnchor = 0.0;

    double stepsPerBeat = std::pow(2.0, (double)rate - 1.0);
    double total = stepsPerBeat * beatsSinceAnchor;

    long tf = (long)std::floor(total);
    float phase = (float)(total - (double)tf);
    // (float) of a fraction just under 1 can round up to exactly 1.0f; that is the next
    // step at phase 0. Keep phase in [0, 1).
    if (phase >= 1.0f)
    {
        phase = 0.f;
        tf += 1;
    }

    StepLFOSongPos r;
    r.step = (int)(((tf % repeat) + repeat) % repeat);
    r.phase = phase;
    return r;
}
} // namespace baconpaul::twofilters
#endif // BACONPAUL_TWOFILTERS_ENGINE_STEPLFO_SONGPOS_H
