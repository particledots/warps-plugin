// JUCE-free wrapper around the Mutable Instruments Warps DSP (MIT licensed, Emilie Gillet).
// Warps runs at 96 kHz with 60-frame blocks and 16-bit samples; this wrapper converts to and from the host rate.
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "Resampler.h"

namespace warps { class Modulator; }

class WarpsEngine
{
public:
    enum Carrier { External = 0, Internal1 = 1, Internal2 = 2, Internal3 = 3 };
    enum Mode { Normal = 0, FrequencyShifter = 1 };

    struct Params
    {
        // Same meaning as the module's controls (all knobs 0..1 unless noted).
        float level1 = 0.5f, level2 = 0.5f;     // channel drive (squared internally, like the module)
        float algorithm = 0.0f;                 // the big knob
        float timbre = 0.5f;                    // the "parameter" knob
        float oscNote = 60.0f;                  // internal oscillator pitch, MIDI note number (fractional ok)
        int carrier = External;                 // 0 = external carrier, 1..3 = internal oscillator shapes
        int mode = Normal;                      // Normal or the module's hidden frequency-shifter mode
    };

    WarpsEngine();
    ~WarpsEngine();

    void prepare (double hostSampleRate);

    // carrier/modulator are the module's input 1 and input 2. outMain/outAux are the module's two outputs.
    void process (const float* carrier, const float* modulator, float* outMain, float* outAux,
                  int numSamples, const Params& p);

    int latencyInHostSamples() const;
    double processorRate() const { return procRate; }

private:
    struct Frame { float c, m; };

    void runBlocks (const Params& p);

    std::unique_ptr<warps::Modulator> mod;

    double hostRate = 48000.0, procRate = 96000.0;
    static constexpr size_t kBlock = 60;   // the module's block size

    SincResampler rsInC, rsInM, rsOutMain, rsOutAux;
    std::vector<float> tmpC, tmpM;
    std::vector<float> hostQMain, hostQAux;
    size_t hostQHead = 0;
    std::vector<Frame> inQueue;
    size_t inQueueHead = 0;
    std::vector<float> procOutMain, procOutAux;
};
