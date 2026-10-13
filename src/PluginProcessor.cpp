#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::AudioProcessorValueTreeState::ParameterLayout WarpsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f, 0.0001f), def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };

    p.push_back (unit ("level1",    "Level 1",   0.5f));
    p.push_back (unit ("level2",    "Level 2",   0.5f));
    p.push_back (unit ("algorithm", "Algorithm", 0.0f));
    p.push_back (unit ("timbre",    "Timbre",    0.5f));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "osc_note", 1 }, "Osc Note",
                 juce::NormalisableRange<float> (0.f, 127.f, 0.01f), 60.f,
                 juce::AudioParameterFloatAttributes().withLabel ("note")));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "osc_tune", 1 }, "Osc Tune",
                 juce::NormalisableRange<float> (-12.f, 12.f, 0.01f), 0.f,
                 juce::AudioParameterFloatAttributes().withLabel ("st")));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "follow_midi", 1 }, "Osc Follows MIDI", true));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "carrier", 1 }, "Carrier",
                 juce::StringArray { "External", "Internal 1 (sine / saw)", "Internal 2 (triangle / pulse)", "Internal 3 (saw / noise)" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "mode", 1 }, "Mode",
                 juce::StringArray { "Normal", "Frequency shifter" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "input_source", 1 }, "Modulator Source",
                 juce::StringArray { "Auto (sidechain if connected)", "Sidechain", "Main input: L = carrier, R = modulator" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "output_mode", 1 }, "Output",
                 juce::StringArray { "Main on both channels", "Main left, Aux right" }, 0));
    return { p.begin(), p.end() };
}

WarpsAudioProcessor::WarpsAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool WarpsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (l.getMainInputChannelSet() != out)
        return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void WarpsAudioProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.prepare (sr);
    carrierBuf.assign ((size_t) maxBlock, 0.f);
    modBuf.assign ((size_t) maxBlock, 0.f);
    mainOut.assign ((size_t) maxBlock, 0.f);
    auxOut.assign ((size_t) maxBlock, 0.f);
    lastMidiNote = -1;
    setLatencySamples (engine.latencyInHostSamples());
}

void WarpsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if ((size_t) n > carrierBuf.size())
    {   // host exceeded its promised max block size
        carrierBuf.resize ((size_t) n); modBuf.resize ((size_t) n);
        mainOut.resize ((size_t) n);    auxOut.resize ((size_t) n);
    }

    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // MIDI: the last note played sets the internal oscillator's pitch (when "Osc Follows MIDI" is on).
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) lastMidiNote = msg.getNoteNumber();
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { /* keep last pitch, like a held CV */ }
    }
    midi.clear();

    WarpsEngine::Params prm;
    prm.level1 = v ("level1"); prm.level2 = v ("level2");
    prm.algorithm = v ("algorithm"); prm.timbre = v ("timbre");
    prm.carrier = (int) v ("carrier");
    prm.mode = (int) v ("mode");
    const bool follow = v ("follow_midi") > 0.5f;
    prm.oscNote = ((follow && lastMidiNote >= 0) ? (float) lastMidiNote : v ("osc_note")) + v ("osc_tune");

    // Work out where the module's two inputs come from.
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int mainCh = mainIn.getNumChannels();
    const int srcMode = (int) v ("input_source");   // 0 auto, 1 sidechain, 2 main L/R

    bool scAvailable = false;
    if (auto* scBus = getBus (true, 1))
        scAvailable = scBus->isEnabled() && scBus->getNumberOfChannels() > 0;

    const bool useSidechain = scAvailable && srcMode != 2;   // Auto or Sidechain, and a sidechain is actually connected

    if (useSidechain)
    {
        // carrier = main input (summed to mono), modulator = sidechain (summed to mono)
        for (int i = 0; i < n; ++i)
        {
            float c = 0.f;
            for (int ch = 0; ch < mainCh; ++ch) c += mainIn.getReadPointer (ch)[i];
            carrierBuf[(size_t) i] = c / (float) juce::jmax (1, mainCh);
        }
        auto scIn = getBusBuffer (buffer, true, 1);
        const int scCh = scIn.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float m = 0.f;
            for (int ch = 0; ch < scCh; ++ch) m += scIn.getReadPointer (ch)[i];
            modBuf[(size_t) i] = m / (float) juce::jmax (1, scCh);
        }
    }
    else
    {
        // "Sidechain" chosen but not connected: modulator stays silent so it's obvious. Otherwise L = carrier, R = modulator.
        const bool silentMod = (srcMode == 1);
        for (int i = 0; i < n; ++i)
        {
            carrierBuf[(size_t) i] = mainIn.getReadPointer (0)[i];
            modBuf[(size_t) i] = silentMod ? 0.f : mainIn.getReadPointer (mainCh > 1 ? 1 : 0)[i];
        }
    }

    engine.process (carrierBuf.data(), modBuf.data(), mainOut.data(), auxOut.data(), n, prm);

    auto out = getBusBuffer (buffer, false, 0);
    const int outCh = out.getNumChannels();
    const bool auxOnRight = ((int) v ("output_mode") == 1);
    out.copyFrom (0, 0, mainOut.data(), n);
    if (outCh > 1)
        out.copyFrom (1, 0, auxOnRight ? auxOut.data() : mainOut.data(), n);
}

juce::AudioProcessorEditor* WarpsAudioProcessor::createEditor()
{
    return new WarpsEditor (*this);
}

void WarpsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void WarpsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WarpsAudioProcessor(); }
