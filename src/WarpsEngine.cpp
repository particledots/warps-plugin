#include "WarpsEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include "warps/dsp/modulator.h"
#include "warps/resources.h"
#include "stmlib/dsp/dsp.h"

namespace
{
    // The firmware's modulator lives in global memory, which the chip zeroes at start-up, and parts of Warps
    // (for example the input noise gate's level) are never initialised by the code itself. So hand it zeroed memory too.
    template <class T> T* newZeroed()
    {
        void* m = ::operator new (sizeof (T));
        std::memset (m, 0, sizeof (T));
        return new (m) T();
    }
}

float WarpsEngine::snapPitch (float semitones, int mode)
{
    if (mode == SnapSemitones) return std::round (semitones);
    if (mode != SnapSteps) return semitones;
    static const float kSteps[] = { 0.f, 7.f, 12.f, 19.f, 24.f, 31.f, 36.f, 43.f, 48.f, 55.f, 60.f, 67.f, 72.f, 79.f, 84.f,
                                    91.f, 96.f, 103.f, 108.f, 115.f, 120.f, 127.f };   // mirrored below zero
    const float a = std::fabs (semitones);
    float best = kSteps[0];
    for (float d : kSteps)
        if (std::fabs (a - d) < std::fabs (a - best)) best = d;
    return semitones < 0.f ? -best : best;
}

float WarpsEngine::oscPitch (float baseNote, float tune, bool followsMidi, int snapMode)
{
    if (snapMode == SnapOff) return baseNote + tune;
    return followsMidi ? baseNote + snapPitch (tune, snapMode) : snapPitch (baseNote + tune, snapMode);
}

WarpsEngine::WarpsEngine() {}
WarpsEngine::~WarpsEngine() = default;

void WarpsEngine::prepare (double sr)
{
    hostRate = sr;
    procRate = 96000.0;
    mod.reset (newZeroed<warps::Modulator>());   // fresh, zeroed state every time we (re)prepare
    mod->Init (96000.0f);
    rsInC.init (hostRate, procRate);   rsInM.init (hostRate, procRate);
    rsOutMain.init (procRate, hostRate); rsOutAux.init (procRate, hostRate);
    inQueue.clear(); inQueueHead = 0;
    hostQMain.clear(); hostQAux.clear(); hostQHead = 0;
    // prime the output with silence so the output resampler never starves
    const size_t prime = kBlock * 2 + SincResampler::kTaps;
    procOutMain.assign (prime, 0.0f); procOutAux.assign (prime, 0.0f);
}

int WarpsEngine::latencyInHostSamples() const
{
    const double procFrames = (double) kBlock * 2 + SincResampler::kTaps;   // block + priming
    if (std::abs (hostRate - procRate) < 1e-6) return (int) procFrames;
    return (int) std::ceil (procFrames * hostRate / procRate);
}

static inline short toShort (float x)
{
    const float v = std::clamp (x, -1.0f, 1.0f) * 32767.0f;
    return (short) std::lrintf (v);
}

void WarpsEngine::runBlocks (const Params& p)
{
    using namespace warps;
    const size_t B = kBlock;

    // Map the plug-in's knobs to the module's internal parameters, as cv_scaler.cc does when nothing is patched.
    warps::Parameters* prm = mod->mutable_parameters();
    prm->channel_drive[0] = p.level1 * p.level1;
    prm->channel_drive[1] = p.level2 * p.level2;

    const float algoPot = std::clamp (p.algorithm, 0.0f, 1.0f);
    float algo = stmlib::Interpolate (lut_pot_curve, algoPot, 512.0f);    // the module's pot taper
    if (algo <= 0.125f)
        algo = std::clamp (algo * 1.08f - 0.01f, 0.0f, 1.0f);             // upstream anti-bleed tweak at the low end
    prm->modulation_algorithm = std::clamp (algo, 0.0f, 1.0f);
    prm->modulation_parameter = std::clamp (p.timbre, 0.0f, 1.0f);

    // Frequency-shifter ("easter egg") controls use the raw pot position.
    prm->frequency_shift_pot = algoPot;
    prm->frequency_shift_cv = 0.0f;
    prm->phase_shift = algoPot;

    prm->note = p.oscNote;
    prm->carrier_shape = p.carrier;
    mod->set_easter_egg (p.mode == FrequencyShifter);

    std::vector<ShortFrame> in (B), out (B);
    while (inQueue.size() - inQueueHead >= B)
    {
        for (size_t i = 0; i < B; ++i)
        {
            const Frame& f = inQueue[inQueueHead + i];
            in[i].l = toShort (f.c);    // module input 1 (carrier / phase-mod input)
            in[i].r = toShort (f.m);    // module input 2 (modulator)
        }
        inQueueHead += B;

        mod->Process (in.data(), out.data(), B);

        for (size_t i = 0; i < B; ++i)
        {
            procOutMain.push_back ((float) out[i].l / 32768.0f);
            procOutAux.push_back  ((float) out[i].r / 32768.0f);
        }
    }
    if (inQueueHead > 4096)
    {
        inQueue.erase (inQueue.begin(), inQueue.begin() + (long) inQueueHead);
        inQueueHead = 0;
    }
}

void WarpsEngine::process (const float* carrier, const float* modulator, float* outMain, float* outAux,
                           int n, const Params& p)
{
    // 1. host rate -> 96 kHz
    tmpC.clear(); tmpM.clear();
    rsInC.process (carrier, (size_t) n, tmpC);
    rsInM.process (modulator, (size_t) n, tmpM);
    const size_t produced = std::min (tmpC.size(), tmpM.size());
    for (size_t i = 0; i < produced; ++i)
        inQueue.push_back ({ tmpC[i], tmpM[i] });

    // 2. run the module DSP
    runBlocks (p);

    // 3. 96 kHz -> host rate
    rsOutMain.process (procOutMain.data(), procOutMain.size(), hostQMain);
    rsOutAux.process  (procOutAux.data(),  procOutAux.size(),  hostQAux);
    procOutMain.clear(); procOutAux.clear();

    const size_t avail = std::min (hostQMain.size(), hostQAux.size()) - hostQHead;
    const size_t take = std::min (avail, (size_t) n);
    for (size_t i = 0; i < take; ++i) { outMain[i] = hostQMain[hostQHead + i]; outAux[i] = hostQAux[hostQHead + i]; }
    for (size_t i = take; i < (size_t) n; ++i) { outMain[i] = 0.0f; outAux[i] = 0.0f; }   // underrun guard
    hostQHead += take;
    if (hostQHead > 8192)
    {
        hostQMain.erase (hostQMain.begin(), hostQMain.begin() + (long) hostQHead);
        hostQAux.erase  (hostQAux.begin(),  hostQAux.begin()  + (long) hostQHead);
        hostQHead = 0;
    }
}
