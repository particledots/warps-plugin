# Unofficial Warps — AU / VST3 port (work in progress)

A plugin wrapper around the Mutable Instruments **Warps** DSP, taken from the open-source release
(`pichenettes/eurorack`, `warps/dsp` + `warps/resources.*`, plus `stmlib`). Not a Mutable Instruments product.
Don't contact Emilie Gillet or Mutable Instruments about it.

## Easiest: let GitHub build it
`.github/workflows/build.yml` builds the AU and VST3 on a macOS runner (universal arm64 + x86_64, macOS 11+).
Download `Warps-macOS` from the latest run's Artifacts (Actions tab). `INSTALL.txt` inside covers the install.

## Build it yourself (macOS)
```
brew install cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
It shows up in Logic as "particledots: Unofficial Warps". The AU is declared as a *MIDI-controlled effect* (`aumf`),
so the last MIDI note you play can set the internal oscillator's pitch.

Headless DSP test (any OS): `cmake --build build --target engine_test && build/engine_test`

## Inputs and outputs

Warps has two audio inputs: **carrier** (input 1) and **modulator** (input 2).

| Plugin | Meaning |
|---|---|
| Main input | the carrier (a stereo input is summed to mono) |
| Sidechain input | the modulator |
| Modulator Source = Auto | use the sidechain if one is connected, otherwise main L = carrier, main R = modulator |
| Modulator Source = Sidechain | sidechain only; if none is connected the modulator is silent |
| Modulator Source = Main L/R | main left = carrier, main right = modulator |
| Output = Main on both channels | the module's main output on L and R |
| Output = Main left, Aux right | main output on L, the module's aux output on R |

With an **internal** carrier (Carrier menu), the module's input 1 becomes the phase-modulation input of its internal
oscillator, and the oscillator is pitched by **Osc Note** + **Osc Tune** (or by the last MIDI note if "Osc Follows MIDI" is on).

## Parameters

| Plugin | Module |
|---|---|
| Level 1, Level 2 | the two channel level knobs (squared internally, as the module does when nothing is patched) |
| Algorithm | the big algorithm knob, through the module's own pot taper. Cross-fades through crossfade, fold, analog ring mod, digital ring mod, XOR, comparator, then the vocoder |
| Timbre | the parameter knob (formant shift in the vocoder) |
| Carrier | External, or one of the three internal oscillator shapes (the module's switch) |
| Mode | Normal, or the module's hidden frequency-shifter mode |
| Osc Note / Osc Tune / Osc Follows MIDI | pitch of the internal oscillator (on the module this is the Level 1 knob / V/oct input) |

At the very top of the Algorithm knob the vocoder **freezes** the spectrum it has captured (upstream behaviour). From a cold start it is silent
until the vocoder has heard something, so turn the knob up to just below the top first, then to the top.

## How it works / honest limits

* The module DSP is called exactly as the firmware does: 96 kHz, 60-frame blocks, 16-bit samples. A windowed-sinc
  resampler converts to and from the host rate. This adds a little latency (reported to the host, roughly 1.5 ms at 48 kHz).
* The module's 16-bit input/output path is kept, so there is 16-bit quantisation noise, as on the hardware.
* The module's CV inputs, calibration, normalisation detection and the pot/UI code are left out. Level knobs and the
  Algorithm knob are reached without any CV.
* **Verified here (Linux, October 2026):** the Warps DSP and wrapper compile with g++ and run at 44.1 / 48 / 96 kHz across all algorithms,
  the three internal oscillators and the frequency-shifter mode, with finite, bounded, non-silent output (`engine_test`).
  The plugin source files compile against the JUCE 8.0.6 headers without warnings.
* **Not verified:** the macOS/AU build (until the GitHub run passes), loading in Logic on either Mac, whether Logic offers a sidechain menu for a
  MIDI-controlled-effect AU, the MIDI note to pitch behaviour in a real host, and **how closely it sounds like a real Warps**. No A/B against a module
  has been done. Use the "Main L/R" source mode if the sidechain does not work in Logic.
* The UI is a basic custom panel. It is functional rather than polished.

## Where the source came from

* `third_party/warps/` is `warps/dsp/*` and `warps/resources.*` from `pichenettes/eurorack`, commit `f4cfa7697095a28d292bff758beb51deb49b5a86`.
  One change: the hardware-only `#include "warps/drivers/debug_pin.h"` line was removed from `modulator.cc` (it is not used on a desktop build).
* `third_party/stmlib/` is the subset of `pichenettes/stmlib` at commit `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` that Warps needs
  (the version the `eurorack` repo pins).

## Licensing

`third_party/` is MIT-licensed code by Emilie Gillet (STM32F projects in the upstream repo are MIT; copyright notices are kept in the file headers;
see `third_party/stmlib/LICENSE`). Keep those notices if you redistribute. The hardware design is CC-BY-SA; none of it is used here.
The wrapper code in `src/` has no licence assigned yet. JUCE is used under its own licence terms (AGPLv3 / commercial); check them before distributing builds.
