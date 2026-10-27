#pragma once
#include "../Metering/XefMeteringEngine.h"
#include "XefTheme.h"

namespace xef::ui
{
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (MeteringEngine& e) : engine (e)
    {
        for (auto* c : { &fftBox }) addAndMakeVisible (c);
        fftBox.addItem ("2048", 11); fftBox.addItem ("4096", 12); fftBox.addItem ("8192", 13);
        fftBox.setSelectedId (juce::jlimit (11, 13, engine.settings.fftOrder.load()), juce::dontSendNotification);
        fftBox.onChange = [this] { engine.settings.fftOrder.store (fftBox.getSelectedId()); };

        addAndMakeVisible (avg); avg.setRange (0.0, 0.95, 0.01); avg.setSliderStyle (juce::Slider::LinearHorizontal);
        avg.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); avg.setTooltip ("Averaging / smoothing");
        avg.setValue (engine.settings.spectrumAveraging.load(), juce::dontSendNotification);
        avg.onValueChange = [this] { engine.settings.spectrumAveraging.store ((float) avg.getValue()); };
        avg.setColour (juce::Slider::thumbColourId, Theme::hot()); avg.setColour (juce::Slider::trackColourId, Theme::accent());

        for (auto* t : { &holdBtn, &inBtn }) { addAndMakeVisible (t); t->setClickingTogglesState (true); }
        holdBtn.setButtonText ("HOLD"); holdBtn.setToggleState (engine.settings.peakHoldTrace.load(), juce::dontSendNotification);
        holdBtn.onClick = [this] { engine.settings.peakHoldTrace.store (holdBtn.getToggleState()); };
        inBtn.setButtonText ("IN"); inBtn.setToggleState (engine.settings.inputOverlay.load(), juce::dontSendNotification);
        inBtn.onClick = [this] { engine.settings.inputOverlay.store (inBtn.getToggleState()); };
        addAndMakeVisible (resetHoldBtn); resetHoldBtn.setButtonText ("CLR");
        resetHoldBtn.onClick = [this] { outProc.resetHold(); inProc.resetHold(); };

        applyRate();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        auto strip = b.removeFromTop (18).withTrimmedLeft (60);
        fftBox.setBounds (strip.removeFromRight (62)); strip.removeFromRight (4);
        resetHoldBtn.setBounds (strip.removeFromRight (36)); strip.removeFromRight (4);
        holdBtn.setBounds (strip.removeFromRight (44)); strip.removeFromRight (4);
        inBtn.setBounds (strip.removeFromRight (34)); strip.removeFromRight (8);
        avg.setBounds (strip.removeFromRight (90));
        plot = b.withTrimmedLeft (30).withTrimmedBottom (14).withTrimmedTop (4).toFloat();
    }

    void tick()
    {
        if (engine.sampleRate.load() != rate) applyRate();
        const int order = engine.settings.fftOrder.load();
        outProc.setFftOrder (order); inProc.setFftOrder (order);
        const float a = engine.settings.spectrumAveraging.load(), f = engine.settings.spectrumFallDb.load();
        for (auto* p : { &outProc, &inProc }) { p->setAveraging (a); p->setFallDbPerFrame (f); }
        outProc.update (engine.outSpec.ring);
        if (engine.settings.inputOverlay.load()) inProc.update (engine.inSpec.ring); else engine.inSpec.ring.discardAll();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "SPECTRUM  dBFS");
        auto xOf = [this] (float hz) { return plot.getX() + plot.getWidth() * std::log10 (hz / 20.0f) / 3.0f; };
        auto yOf = [this] (float db) { return juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, plot.getBottom(), plot.getY()); };

        struct Region { float a, b; const char* name; } regions[] = { { 20.0f, 250.0f, "LOW" }, { 250.0f, 4000.0f, "MID" }, { 4000.0f, 20000.0f, "HIGH" } };
        int i = 0;
        for (auto& r : regions)
        {
            g.setColour (Theme::accent().withAlpha (i++ % 2 ? 0.05f : 0.02f)); g.fillRect (xOf (r.a), plot.getY(), xOf (r.b) - xOf (r.a), plot.getHeight());
            g.setColour (Theme::dim()); g.setFont (9.0f);
            g.drawText (r.name, juce::Rectangle<float> (xOf (r.a), plot.getY(), xOf (r.b) - xOf (r.a), 11.0f), juce::Justification::centred, false);
        }
        g.setFont (9.0f);
        for (float db = 0.0f; db >= -90.0f; db -= 15.0f)
        {
            g.setColour (Theme::grid()); g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
            g.setColour (Theme::dim());  g.drawText (juce::String ((int) db), (int) plot.getX() - 28, (int) yOf (db) - 5, 24, 10, juce::Justification::centredRight, false);
        }
        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            g.setColour (Theme::grid()); g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
            g.setColour (Theme::dim());
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz), (int) xOf (hz) - 16, (int) plot.getBottom() + 1, 32, 11, juce::Justification::centred, false);
        }

        auto build = [&] (const std::array<float, SpectrumProcessor::kPoints>& t, bool close)
        {
            juce::Path p;
            for (int k = 0; k < SpectrumProcessor::kPoints; ++k)
            {
                const float x = plot.getX() + plot.getWidth() * ((float) k + 0.5f) / (float) SpectrumProcessor::kPoints, y = yOf (t[(size_t) k]);
                if (k == 0) { if (close) p.startNewSubPath (x, plot.getBottom()); else p.startNewSubPath (x, y); if (close) p.lineTo (x, y); }
                else p.lineTo (x, y);
            }
            if (close) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
            return p;
        };

        if (engine.settings.inputOverlay.load())
        { g.setColour (Theme::text().withAlpha (0.55f)); g.strokePath (build (inProc.getTrace(), false), juce::PathStrokeType (1.0f)); }

        const auto fill = build (outProc.getTrace(), true), line = build (outProc.getTrace(), false);
        juce::ColourGradient grad (Theme::hot().withAlpha (0.6f), 0, plot.getY(), Theme::accent().withAlpha (0.04f), 0, plot.getBottom(), false);
        grad.addColour (0.35, Theme::accent().withAlpha (0.35f));
        g.setGradientFill (grad); g.fillPath (fill);
        g.setColour (Theme::accent().withAlpha (0.20f)); g.strokePath (line, juce::PathStrokeType (4.0f));
        g.setColour (Theme::hot()); g.strokePath (line, juce::PathStrokeType (1.4f));
        if (engine.settings.peakHoldTrace.load()) { g.setColour (Theme::ember().withAlpha (0.85f)); g.strokePath (build (outProc.getHold(), false), juce::PathStrokeType (1.0f)); }

        g.setColour (Theme::dim()); g.setFont (9.0f);
        g.drawText ("OUT" + juce::String (engine.settings.inputOverlay.load() ? " (fill)  IN (line)" : ""), 8, 4, 140, 10, juce::Justification::centredLeft, false);
    }

private:
    void applyRate()
    {
        rate = engine.sampleRate.load();
        outProc.prepare (rate, engine.settings.fftOrder.load()); inProc.prepare (rate, engine.settings.fftOrder.load());
    }

    MeteringEngine& engine;
    SpectrumProcessor outProc, inProc;
    double rate = 0.0;
    juce::ComboBox fftBox; juce::Slider avg; juce::TextButton holdBtn, inBtn, resetHoldBtn;
    juce::Rectangle<float> plot;
};
} // namespace xef::ui