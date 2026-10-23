// Pitch Snap (our addition): the internal oscillator's pitch settles on octaves and fifths (Steps) or whole semitones (Semitones).
//  1. the snapping function: exact over the whole range; Off returns its input unchanged
//  2. how Osc Note / Osc Tune / the MIDI note combine (WarpsEngine::oscPitch): Off is exactly the old sum; with Osc Follows MIDI
//     only Osc Tune snaps; without it Osc Note + Osc Tune snap as one note
//  3. through the engine: the oscillator's measured pitch lands on the expected note, for each internal carrier
//  4. the internal carriers stay finite while the pitch sweeps in every snap mode
// It says nothing about how anything sounds.
#include "WarpsEngine.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

using E = WarpsEngine;

// Renders `seconds` with a silent carrier input and a silent modulator: the internal oscillator alone (algorithm 0 passes it).
static std::vector<float> render (double sr, double seconds, int carrier, float algorithm, float note)
{
    E e; e.prepare (sr);
    E::Params p; p.carrier = carrier; p.algorithm = algorithm; p.level1 = 0.9f; p.level2 = 0.9f; p.timbre = 0.0f; p.oscNote = note;
    const int N = 512;
    std::vector<float> c (N, 0.f), m (N, 0.f), a (N), b (N), all;
    for (int k = 0; k < (int) (sr * seconds / N); ++k) { e.process (c.data(), m.data(), a.data(), b.data(), N, p); all.insert (all.end(), a.begin(), a.end()); }
    return all;
}

static double peakHz (const std::vector<float>& x0, double sr, size_t from, size_t count, double lo, double hi)
{
    std::vector<float> x (count);
    for (size_t i = 0; i < count; ++i) x[i] = x0[from + i] * (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) count));
    double bestF = 0, bestM = -1;
    for (double f = lo; f < hi; f *= 1.0002)
    {
        const double w = 2.0 * M_PI * f / sr, cr = std::cos (w), ci = std::sin (w);
        double zr = 1.0, zi = 0.0, re = 0.0, im = 0.0;
        for (size_t i = 0; i < count; ++i) { re += x[i] * zr; im += x[i] * zi; const double nr = zr * cr - zi * ci; zi = zr * ci + zi * cr; zr = nr; }
        const double m = re * re + im * im;
        if (m > bestM) { bestM = m; bestF = f; }
    }
    return bestF;
}

int main()
{
    int fails = 0;

    // 1. the function
    {
        static const float steps[] = { 0, 7, 12, 19, 24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84, 91, 96, 103, 108, 115, 120, 127 };
        bool ok = true; float prev = -1000.f; int distinct = 0; float last = 1e9f;
        for (int i = -14000; i <= 14000; ++i)
        {
            const float k = (float) i * 0.01f;
            const float st = E::snapPitch (k, E::SnapSteps);
            const float off = E::snapPitch (k, E::SnapOff);
            float bestDist = 1e9f; for (float s : steps) bestDist = std::min (bestDist, std::fabs (std::fabs (k) - s));
            bool onStep = false; for (float s : steps) onStep = onStep || std::fabs (std::fabs (st) - s) < 1e-6f;
            ok = ok && onStep && st >= prev - 1e-6f && std::fabs (std::fabs (st) - std::fabs (k)) <= bestDist + 1e-4f
                    && (st == 0.f || (st < 0.f) == (k < 0.f))
                    && std::fabs (E::snapPitch (k, E::SnapSemitones) - std::round (k)) < 1e-6f
                    && std::memcmp (&k, &off, sizeof (float)) == 0;
            prev = st; if (st != last) { ++distinct; last = st; }
        }
        ok = ok && distinct == 43;
        printf ("snap function over -140..+140: Steps visit exactly 43 values (%d seen), the nearest one, never backwards; Semitones round; Off returns the same bits  %s\n",
                distinct, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 2. how the pieces combine
    {
        bool ok = true;
        // Off: exactly base + tune, for awkward values too
        for (float base : { 0.0f, 33.3f, 60.0f, 61.37f, 127.0f })
            for (float tune : { -12.0f, -3.1f, 0.0f, 0.07f, 5.5f, 12.0f })
                for (bool follow : { false, true })
                {
                    const float want = base + tune, got = E::oscPitch (base, tune, follow, E::SnapOff);
                    ok = ok && std::memcmp (&want, &got, sizeof (float)) == 0;
                }
        struct C { float base, tune; bool follow; int snap; float want; };
        const C cs[] = {
            { 60.f,  3.0f, true,  E::SnapSteps,     60.f },   // following: only the tune snaps (3 -> 0)
            { 62.f,  4.0f, true,  E::SnapSteps,     69.f },   //            4 -> 7
            { 57.f, -10.f, true,  E::SnapSteps,     45.f },   //          -10 -> -12
            { 64.f, 11.0f, true,  E::SnapSteps,     76.f },   //           11 -> 12
            { 61.f,  2.6f, true,  E::SnapSemitones, 64.f },   //          2.6 -> 3
            { 60.4f, 0.3f, false, E::SnapSteps,     60.f },   // free: Osc Note + Osc Tune snap as one note, 60.7 -> 60
            { 62.f,  3.0f, false, E::SnapSteps,     67.f },   // 65 -> 67
            { 33.f, -1.0f, false, E::SnapSteps,     31.f },   // 32 -> 31
            { 127.f, 12.f, false, E::SnapSteps,    127.f },   // 139 -> the top step, 127
            { 60.4f, 0.3f, false, E::SnapSemitones, 61.f },   // 60.7 -> 61
            { 33.f, -1.6f, false, E::SnapSemitones, 31.f } }; // 31.4 -> 31
        for (const auto& c : cs)
        {
            const float got = E::oscPitch (c.base, c.tune, c.follow, c.snap);
            if (std::fabs (got - c.want) > 1e-4f) { ok = false; printf ("  oscPitch(%.2f, %.2f, follow=%d, snap=%d) = %.3f, want %.3f\n", c.base, c.tune, (int) c.follow, c.snap, got, c.want); }
        }
        printf ("oscPitch: Off is exactly base + tune; with Osc Follows MIDI only the tune snaps; without it note + tune snap as one  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 3. through the engine: the measured pitch of each internal carrier
    {
        struct C { float base, tune; bool follow; int snap; float wantNote; };
        const C cs[] = { { 60.f, 3.0f, true, E::SnapSteps, 60.f }, { 62.f, 4.0f, true, E::SnapSteps, 69.f }, { 57.f, -10.f, true, E::SnapSteps, 45.f },
                         { 61.f, 2.6f, true, E::SnapSemitones, 64.f }, { 60.4f, 0.3f, false, E::SnapSteps, 60.f }, { 62.f, 3.0f, false, E::SnapSteps, 67.f },
                         { 60.4f, 0.3f, false, E::SnapSemitones, 61.f }, { 60.f, 3.0f, true, E::SnapOff, 63.f }, { 60.f, 0.5f, false, E::SnapOff, 60.5f } };
        bool ok = true; double worst = 0;
        for (double sr : { 44100.0, 48000.0 })
            for (int carrier = 1; carrier <= 3; ++carrier)
                for (const auto& c : cs)
                {
                    const float note = E::oscPitch (c.base, c.tune, c.follow, c.snap);
                    const auto a = render (sr, 3.0, carrier, 0.0f, note);
                    const double want = 440.0 * std::pow (2.0, ((double) c.wantNote - 69.0) / 12.0);
                    const double got = peakHz (a, sr, (size_t) (sr * 2.0), 32768, want / 1.4, want * 1.4);
                    const double cents = 1200.0 * std::log2 (got / want);
                    worst = std::max (worst, std::fabs (cents));
                    if (std::fabs (cents) >= 5.0) { ok = false; printf ("  carrier %d sr %.0f base %.2f tune %.2f follow %d snap %d: want %.2f Hz got %.2f Hz (%+.1f cents)\n", carrier, sr, c.base, c.tune, (int) c.follow, c.snap, want, got, cents); }
                }
        printf ("pitch snap through the engine: %d settings x 3 internal carriers x 2 sample rates land on the expected note; worst error %.2f cents  %s\n",
                (int) (sizeof (cs) / sizeof (cs[0])), worst, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 4. every carrier, every snap mode, pitch sweeping across the whole range
    {
        bool ok = true;
        for (int snap : { (int) E::SnapOff, (int) E::SnapSteps, (int) E::SnapSemitones })
            for (int carrier = 1; carrier <= 3; ++carrier)
                for (bool follow : { false, true })
                {
                    E e; e.prepare (48000.0);
                    E::Params p; p.carrier = carrier; p.algorithm = 0.0f; p.level1 = 0.9f; p.level2 = 0.9f;
                    const int N = 512; std::vector<float> c (N, 0.f), m (N, 0.f), a (N), b (N);
                    bool fin = true; double en = 0, pk = 0;
                    for (int k = 0; k < 188; ++k)
                    {
                        p.oscNote = E::oscPitch (63.5f + 63.5f * (float) std::sin (k * 0.09), 12.0f * (float) std::sin (k * 0.31), follow, snap);
                        e.process (c.data(), m.data(), a.data(), b.data(), N, p);
                        for (int i = 0; i < N; ++i) { fin = fin && std::isfinite (a[i]) && std::isfinite (b[i]); en += (double) a[i] * a[i]; pk = std::max (pk, (double) std::fabs (a[i])); }
                    }
                    if (! (fin && en > 0.05 && pk <= 1.5)) { ok = false; printf ("  snap %d carrier %d follow %d: finite=%d energy=%.2f peak=%.2f\n", snap, carrier, (int) follow, (int) fin, en, pk); }
                }
        printf ("internal carriers x 3 snap modes x follow on/off stay finite, audible and bounded while the pitch sweeps  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    printf ("%d failed\n", fails);
    return fails;
}
