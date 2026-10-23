#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple flat look for rotary knobs.
class WarpsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WarpsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class WarpsEditor : public juce::AudioProcessorEditor
{
public:
    explicit WarpsEditor (WarpsAudioProcessor&);
    ~WarpsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    struct Menu
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void addMenu (Menu&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items);

    WarpsAudioProcessor& proc;
    WarpsLookAndFeel laf;

    Knob level1, level2, algorithm, timbre, oscNote, oscTune;
    Menu carrier, mode, source, output, pitchSnap;
    std::unique_ptr<juce::ParameterAttachment> snapReadoutAtt, followReadoutAtt, tuneReadoutAtt;   // keep the pitch readouts in step
    juce::ToggleButton followMidi { "OSC FOLLOWS MIDI" };
    std::unique_ptr<ButtonAtt> followAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarpsEditor)
};
