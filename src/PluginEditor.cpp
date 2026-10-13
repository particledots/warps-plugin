#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff1a1c20);
    const juce::Colour kPanel   (0xff23262c);
    const juce::Colour kTrack   (0xff3a3e47);
    const juce::Colour kAccent  (0xff6fb8c9);
    const juce::Colour kText    (0xffe8e6e1);
    const juce::Colour kDim     (0xff8d909a);
}

WarpsLookAndFeel::WarpsLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTrack);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void WarpsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float thick = juce::jmax (3.0f, d * 0.09f);
    const auto arcR = r.reduced (thick * 0.5f);

    juce::Path track;
    track.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0, a0, a1, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = a0 + pos * (a1 - a0);
    // bipolar controls fill from the centre, others from the start
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path val;
    val.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0,
                       juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    const auto c = r.getCentre();
    const float len = d * 0.5f - thick * 2.2f;
    juce::Line<float> line (c.getPointOnCircumference (len * 0.35f, angle),
                            c.getPointOnCircumference (len, angle));
    g.setColour (kText);
    g.drawLine (line, juce::jmax (2.0f, d * 0.045f));
}

void WarpsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kAccent : kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? kAccent : kTrack);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? juce::Colours::black : kText);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

WarpsEditor::WarpsEditor (WarpsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    addKnob (level1,    "level1",    "LEVEL 1");
    addKnob (level2,    "level2",    "LEVEL 2");
    addKnob (algorithm, "algorithm", "ALGORITHM");
    addKnob (timbre,    "timbre",    "TIMBRE");
    addKnob (oscNote,   "osc_note",  "OSC NOTE");
    addKnob (oscTune,   "osc_tune",  "OSC TUNE");

    addMenu (carrier, "carrier",      "CARRIER",
             { "External", "Internal 1 (sine / saw)", "Internal 2 (triangle / pulse)", "Internal 3 (saw / noise)" });
    addMenu (mode,    "mode",         "MODE", { "Normal", "Frequency shifter" });
    addMenu (source,  "input_source", "MODULATOR SOURCE",
             { "Auto (sidechain if connected)", "Sidechain", "Main input: L = carrier, R = modulator" });
    addMenu (output,  "output_mode",  "OUTPUT", { "Main on both channels", "Main left, Aux right" });

    addAndMakeVisible (followMidi);
    followAtt = std::make_unique<ButtonAtt> (proc.apvts, "follow_midi", followMidi);

    setSize (620, 470);
}

WarpsEditor::~WarpsEditor() { setLookAndFeel (nullptr); }

void WarpsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void WarpsEditor::addMenu (Menu& m, const juce::String& id, const juce::String& name, const juce::StringArray& items)
{
    m.box.addItemList (items, 1);
    m.label.setText (name, juce::dontSendNotification);
    m.label.setJustificationType (juce::Justification::centredLeft);
    m.label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (m.box);
    addAndMakeVisible (m.label);
    m.att = std::make_unique<ComboAtt> (proc.apvts, id, m.box);
}

void WarpsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("UNOFFICIAL WARPS", 20, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("meta-modulator - community port, not a Mutable Instruments product", 20, 34, 520, 16, juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (12.0f, 58.0f, (float) getWidth() - 24.0f, 150.0f, 10.0f);    // main knobs
    g.fillRoundedRectangle (12.0f, 216.0f, (float) getWidth() - 24.0f, 120.0f, 10.0f);   // oscillator
    g.fillRoundedRectangle (12.0f, 344.0f, (float) getWidth() - 24.0f, 118.0f, 10.0f);   // routing
}

void WarpsEditor::resized()
{
    const int W = getWidth();

    auto place = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    // Row 1: the four module knobs
    const int colW = (W - 24) / 4;
    Knob* row1[4] = { &level1, &algorithm, &timbre, &level2 };   
    for (int i = 0; i < 4; ++i)
        place (*row1[i], juce::Rectangle<int> (12 + i * colW, 62, colW, 142).reduced (14, 0));

    // Row 2: internal oscillator pitch
    place (oscNote, juce::Rectangle<int> (12, 220, colW, 112).reduced (14, 0));
    place (oscTune, juce::Rectangle<int> (12 + colW, 220, colW, 112).reduced (14, 0));
    followMidi.setBounds (12 + 2 * colW + 14, 262, colW * 2 - 28, 34);

    // Row 3: menus, two per line
    const int halfW = (W - 24 - 36) / 2;
    auto placeMenu = [&] (Menu& m, int x, int y)
    {
        m.label.setBounds (x, y, halfW, 16);
        m.box.setBounds (x, y + 18, halfW, 26);
    };
    placeMenu (carrier, 24, 352);
    placeMenu (mode,    24 + halfW + 12, 352);
    placeMenu (source,  24, 404);
    placeMenu (output,  24 + halfW + 12, 404);
}
