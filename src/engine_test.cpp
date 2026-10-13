// Offline sanity test of WarpsEngine at several host sample rates, algorithms and carrier modes.
// It checks that the output is finite, bounded and not silent. It does NOT check that it sounds like the module.
#include "WarpsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

int main()
{
    int fails = 0;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double sr : rates)
    {
        // 1) External carrier: sweep the algorithm knob across the whole range.
        for (float algo : { 0.0f, 0.1f, 0.25f, 0.4f, 0.55f, 0.7f, 0.8f, 0.9f, 1.0f })
        {
            WarpsEngine e; e.prepare (sr);
            WarpsEngine::Params p; p.algorithm = algo; p.level1 = 0.9f; p.level2 = 0.9f; p.timbre = 0.5f;
            const int N = 512; std::vector<float> c (N), m (N), main (N), aux (N);
            double pc = 0, pm = 0, energy = 0, maxAbs = 0; bool bad = false;
            const int blocks = (int) (sr * 2 / N);
            for (int b = 0; b < blocks; ++b)
            {
                // At the very top of the knob the vocoder FREEZES its captured spectrum (documented upstream behaviour),
                // so from a cold start it is silent by design. Capture first at 0.9, then jump to 1.0 and expect it to hold.
                if (algo >= 1.0f) p.algorithm = (b < blocks / 4) ? 0.9f : 1.0f;
                for (int i = 0; i < N; ++i)
                {
                    // two saw waves at unrelated pitches: rich in harmonics, so every algorithm (incl. the vocoder) has material
                    pc += 110.0 / sr; if (pc >= 1.0) pc -= 1.0;
                    pm += 150.0 / sr; if (pm >= 1.0) pm -= 1.0;
                    c[i] = 0.4f * (float) (2.0 * pc - 1.0);
                    m[i] = 0.4f * (float) (2.0 * pm - 1.0);
                }
                e.process (c.data(), m.data(), main.data(), aux.data(), N, p);
                for (int i = 0; i < N; ++i)
                {
                    if (! std::isfinite (main[i]) || ! std::isfinite (aux[i])) bad = true;
                    maxAbs = std::max (maxAbs, (double) std::fabs (main[i]));
                    if (b > blocks / 2) energy += main[i] * main[i];
                }
            }
            const bool ok = ! bad && energy > 0.5 && maxAbs <= 1.5;
            printf ("sr=%6.0f  external  algo=%.2f  energy=%9.2f max=%.2f  %s\n", sr, algo, energy, maxAbs, ok ? "OK" : "FAIL");
            fails += ! ok;
        }

        // 2) Internal oscillator (the carrier input becomes a phase-mod input; modulator still comes in).
        for (int shape = 1; shape <= 3; ++shape)
            for (float algo : { 0.1f, 0.5f, 0.95f })
            {
                WarpsEngine e; e.prepare (sr);
                WarpsEngine::Params p; p.carrier = shape; p.algorithm = algo; p.level2 = 0.9f; p.oscNote = 60.0f;
                const int N = 512; std::vector<float> c (N, 0.0f), m (N), main (N), aux (N);
                double pm = 0, energy = 0, maxAbs = 0; bool bad = false;
                const int blocks = (int) (sr * 2 / N);
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < N; ++i) { pm += 220.0 / sr; if (pm >= 1.0) pm -= 1.0; m[i] = 0.4f * (float) (2.0 * pm - 1.0); }
                    e.process (c.data(), m.data(), main.data(), aux.data(), N, p);
                    for (int i = 0; i < N; ++i)
                    {
                        if (! std::isfinite (main[i]) || ! std::isfinite (aux[i])) bad = true;
                        maxAbs = std::max (maxAbs, (double) std::fabs (main[i]));
                        if (b > blocks / 2) energy += main[i] * main[i];
                    }
                }
                const bool ok = ! bad && energy > 0.05 && maxAbs <= 1.5;
                printf ("sr=%6.0f  internal%d algo=%.2f  energy=%9.2f max=%.2f  %s\n", sr, shape, algo, energy, maxAbs, ok ? "OK" : "FAIL");
                fails += ! ok;
            }

        // 3) Hidden frequency-shifter mode, external input.
        {
            WarpsEngine e; e.prepare (sr);
            WarpsEngine::Params p; p.mode = WarpsEngine::FrequencyShifter; p.algorithm = 0.7f; p.level1 = 0.9f; p.level2 = 0.9f;
            const int N = 512; std::vector<float> c (N), m (N), main (N), aux (N);
            double pc = 0, pm = 0, energy = 0, maxAbs = 0; bool bad = false;
            const int blocks = (int) (sr * 2 / N);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < N; ++i) { c[i] = 0.4f * (float) std::sin (pc += 2 * M_PI * 220.0 / sr); m[i] = 0.4f * (float) std::sin (pm += 2 * M_PI * 440.0 / sr); }
                e.process (c.data(), m.data(), main.data(), aux.data(), N, p);
                for (int i = 0; i < N; ++i)
                {
                    if (! std::isfinite (main[i]) || ! std::isfinite (aux[i])) bad = true;
                    maxAbs = std::max (maxAbs, (double) std::fabs (main[i]));
                    if (b > blocks / 2) energy += main[i] * main[i];
                }
            }
            const bool ok = ! bad && energy > 0.5 && maxAbs <= 1.5;
            printf ("sr=%6.0f  freq-shifter           energy=%9.2f max=%.2f  %s\n", sr, energy, maxAbs, ok ? "OK" : "FAIL");
            fails += ! ok;
        }

        WarpsEngine e; e.prepare (sr);
        printf ("sr=%6.0f  processor rate %.0f, reported latency %d samples\n", sr, e.processorRate(), e.latencyInHostSamples());
    }
    printf (fails ? "\n%d FAILURES\n" : "\nall checks passed\n", fails);
    return fails;
}
