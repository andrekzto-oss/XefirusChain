#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace xef::ui
{
struct Theme
{
    static inline std::atomic<bool> crimson { false };
    static juce::Colour accent() { return crimson.load() ? juce::Colour (0xffd01535) : juce::Colour (0xffb44cff); }
    static juce::Colour hot()    { return crimson.load() ? juce::Colour (0xffff3a4a) : juce::Colour (0xffe070ff); }
    static juce::Colour ember()  { return juce::Colour (0xffff8a3d); }
    static juce::Colour warn()   { return juce::Colour (0xffff3030); }
    static juce::Colour bg()     { return juce::Colour (0xff09060c); }
    static juce::Colour panel()  { return juce::Colour (0xff140f19); }
    static juce::Colour metal()  { return juce::Colour (0xff2b242f); }
    static juce::Colour grid()   { return juce::Colour (0xff231a2a); }
    static juce::Colour text()   { return juce::Colour (0xffd8cbe3); }
    static juce::Colour dim()    { return juce::Colour (0xff7d7088); }
};

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& title)
{
    g.setColour (Theme::panel());           g.fillRoundedRectangle (b, 4.0f);
    g.setColour (Theme::metal());           g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Theme::accent().withAlpha (0.55f)); g.fillRect (b.getX() + 6.0f, b.getY() + 1.0f, 18.0f, 1.5f);
    g.setColour (Theme::dim()); g.setFont (10.0f);
    g.drawText (title, b.reduced (8.0f, 3.0f).removeFromTop (13.0f), juce::Justification::centredLeft, false);
}

inline juce::String fmtDb (float v, int dp = 1) { return v <= -119.9f ? juce::String ("-inf") : juce::String (v, dp); }

class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Theme::text());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, Theme::text());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startA, float endA, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto  c = b.getCentre();
        const float angle = startA + pos * (endA - startA);
        const bool  on = s.isEnabled();

        juce::Path track, value;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startA, endA, true);
        value.addCentredArc (c.x, c.y, r, r, 0.0f, startA, angle, true);
        g.setColour (Theme::grid());  g.strokePath (track, juce::PathStrokeType (3.5f));
        if (on)
        {
            g.setColour (Theme::accent().withAlpha (0.25f)); g.strokePath (value, juce::PathStrokeType (8.0f));
            g.setColour (Theme::hot());                       g.strokePath (value, juce::PathStrokeType (3.0f));
        }

        const float br = r * 0.76f;
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillEllipse (c.x - br - 2, c.y - br + 2, 2 * br + 4, 2 * br + 4);
        juce::ColourGradient body (Theme::metal().brighter (0.35f), c.x - br * 0.5f, c.y - br * 0.7f, juce::Colour (0xff0c090e), c.x + br * 0.6f, c.y + br * 0.8f, true);
        g.setGradientFill (body); g.fillEllipse (c.x - br, c.y - br, 2 * br, 2 * br);
        g.setColour (juce::Colours::black); g.drawEllipse (c.x - br, c.y - br, 2 * br, 2 * br, 1.5f);
        g.setColour (Theme::metal().brighter (0.2f).withAlpha (0.5f)); g.drawEllipse (c.x - br * 0.62f, c.y - br * 0.62f, br * 1.24f, br * 1.24f, 1.0f);

        juce::Path tick; tick.addRoundedRectangle (-1.6f, -br + 3.0f, 3.2f, br * 0.46f, 1.5f);
        g.setColour (on ? Theme::hot() : Theme::dim());
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

class ParameterKnob : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& caption, juce::LookAndFeel& laf)
        : attachment (apvts, paramID, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setLookAndFeel (&laf);
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (13.0f);
        addAndMakeVisible (slider); addAndMakeVisible (label);
    }
    ~ParameterKnob() override { slider.setLookAndFeel (nullptr); }
    void resized() override { auto b = getLocalBounds(); label.setBounds (b.removeFromTop (18)); slider.setBounds (b); }

private:
    juce::Slider slider;
    juce::Label  label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};
} // namespace xef::ui