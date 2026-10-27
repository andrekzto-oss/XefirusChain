#pragma once
#include "XefAbState.h"
#include "XefMeterViews.h"
#include "XefSpectrumView.h"
#include "XefTheme.h"

namespace xef::ui
{
class AnalysisZone : public juce::Component
{
public:
    static constexpr int kHeaderH = 24;

    explicit AnalysisZone (MeteringEngine& e) : engine (e), levels (e), loud (e), corr (e), gonio (e), gr (e), spectrum (e)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &levels, &loud, &corr, &gonio, &gr, &spectrum }) addAndMakeVisible (c);
        auto setup = [this] (juce::TextButton& b, const juce::String& t, bool toggle) { addAndMakeVisible (b); b.setButtonText (t); b.setClickingTogglesState (toggle); };
        setup (collapseBtn, "ANALYSIS", false); setup (resetBtn, "RESET HOLD", false); setup (monoBtn, "MONO MON", true); setup (matchBtn, "LEVEL MATCH", true); setup (themeBtn, "THEME", false);
        collapseBtn.onClick = [this] { setCollapsed (! engine.settings.collapsed.load()); if (onLayoutChange) onLayoutChange(); };
        resetBtn.onClick = [this] { engine.resetHolds(); };
        monoBtn.setToggleState (engine.settings.monoMonitor.load(), juce::dontSendNotification);
        monoBtn.onClick = [this] { engine.settings.monoMonitor.store (monoBtn.getToggleState()); };
        matchBtn.setToggleState (engine.settings.levelMatch.load(), juce::dontSendNotification);
        matchBtn.onClick = [this] { engine.settings.levelMatch.store (matchBtn.getToggleState()); };
        themeBtn.onClick = [this] { Theme::crimson.store (! Theme::crimson.load()); if (auto* p = getParentComponent()) p->repaint(); };
        setCollapsed (engine.settings.collapsed.load());
    }

    std::function<void()> onLayoutChange;
    bool isCollapsed() const noexcept { return engine.settings.collapsed.load(); }
    void setCollapsed (bool c)
    {
        engine.settings.collapsed.store (c);
        for (auto* v : std::initializer_list<juce::Component*> { &levels, &loud, &corr, &gonio, &gr, &spectrum }) v->setVisible (! c);
        collapseBtn.setButtonText (c ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8 ANALYSIS")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe ANALYSIS")));
        resized();
    }

    void tick()
    {
        if (isCollapsed() || ! isShowing()) return;
        spectrum.tick(); corr.tick(); gonio.tick(); gr.tick();
        loud.repaint(); levels.repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto head = b.removeFromTop (kHeaderH).reduced (2, 2);
        collapseBtn.setBounds (head.removeFromLeft (96)); head.removeFromLeft (6);
        themeBtn.setBounds (head.removeFromRight (56)); head.removeFromRight (4);
        matchBtn.setBounds (head.removeFromRight (90)); head.removeFromRight (4);
        monoBtn.setBounds (head.removeFromRight (80)); head.removeFromRight (4);
        resetBtn.setBounds (head.removeFromRight (90));
        if (isCollapsed()) return;

        const int gap = 6, rowH = juce::jmax (60, (int) (b.getHeight() * 0.40f));
        auto bottom = b.removeFromBottom (rowH); b.removeFromBottom (gap);
        auto topRow = b;
        gonio.setBounds (topRow.removeFromRight (juce::jmin (topRow.getHeight() + 40, topRow.getWidth() / 3))); topRow.removeFromRight (gap);
        spectrum.setBounds (topRow);

        const int w = bottom.getWidth(); const auto wi = [&] (float f) { return (int) ((float) w * f); };
        loud.setBounds (bottom.removeFromLeft (wi (0.30f)));   bottom.removeFromLeft (gap);
        levels.setBounds (bottom.removeFromLeft (wi (0.24f))); bottom.removeFromLeft (gap);
        corr.setBounds (bottom.removeFromLeft (wi (0.24f)));   bottom.removeFromLeft (gap);
        gr.setBounds (bottom);
    }

private:
    MeteringEngine& engine;
    LevelMetersView levels; LoudnessView loud; CorrelationView corr; GoniometerView gonio; GainReductionView gr; SpectrumView spectrum;
    juce::TextButton collapseBtn, resetBtn, monoBtn, matchBtn, themeBtn;
};

class TopBar : public juce::Component
{
public:
    TopBar (juce::AudioProcessorValueTreeState& apvts, const juce::String& bypassParamID, AbStateManager& abMgr, MeteringEngine& e)
        : ab (abMgr), engine (e), bypassAttachment (apvts, bypassParamID, bypass)
    {
        addAndMakeVisible (presets); presets.setTextWhenNothingSelected ("PRESET");
        addAndMakeVisible (abBtn); abBtn.onClick = [this] { ab.toggle(); };
        addAndMakeVisible (copyBtn); copyBtn.setButtonText ("COPY"); copyBtn.onClick = [this] { ab.copyActiveToOther(); };
        addAndMakeVisible (bypass); bypass.setButtonText ("BYPASS"); bypass.setClickingTogglesState (true);
        ab.onChanged = [this] { refreshAb(); };
        refreshAb();
    }
    ~TopBar() override { ab.onChanged = nullptr; }

    juce::ComboBox presets;

    void tick() { repaint (statusArea); }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 10);
        presets.setBounds (b.removeFromLeft (190)); b.removeFromLeft (10);
        abBtn.setBounds (b.removeFromLeft (46)); b.removeFromLeft (4);
        copyBtn.setBounds (b.removeFromLeft (52)); b.removeFromLeft (10);
        bypass.setBounds (b.removeFromRight (72)); b.removeFromRight (10);
        statusArea = b.removeFromRight (230);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (Theme::panel()); g.fillRect (b);
        g.setColour (Theme::metal()); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        auto logo = juce::Rectangle<float> (b.getCentreX() - 170.0f, 2.0f, 340.0f, b.getHeight() - 4.0f);
        g.setColour (Theme::accent().withAlpha (0.3f)); g.setFont (30.0f); g.drawText ("XEFIRUS CHAIN", logo.translated (0, 1.5f), juce::Justification::centred, false);
        g.setColour (Theme::hot()); g.drawText ("XEFIRUS CHAIN", logo, juce::Justification::centred, false);
        g.setColour (Theme::dim()); g.setFont (9.0f); g.drawText ("MASTER THE DARKNESS", logo.withTop (logo.getBottom() - 12.0f), juce::Justification::centred, false);

        auto s = statusArea.toFloat(); g.setFont (10.0f);
        auto io = [&] (const char* name, const LevelMeter& m, juce::Rectangle<float> r)
        {
            const float pk = juce::jmax (m.holdDb[0].load(), m.holdDb[1].load()); const bool clip = m.clip[0].load() || m.clip[1].load();
            g.setColour (clip ? Theme::warn() : Theme::grid()); g.fillEllipse (r.getX(), r.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (Theme::text()); g.drawText (juce::String (name) + "  " + fmtDb (pk) + " dBFS pk", r.withTrimmedLeft (12.0f), juce::Justification::centredLeft, false);
        };
        io ("IN", engine.inLevel, s.removeFromLeft (s.getWidth() * 0.5f)); io ("OUT", engine.outLevel, s);
    }

private:
    void refreshAb() { abBtn.setButtonText (ab.getActive() == 0 ? "A" : "B"); }
    AbStateManager& ab; MeteringEngine& engine;
    juce::TextButton abBtn, copyBtn, bypass;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;
    juce::Rectangle<int> statusArea;
};

struct KnobSpec { juce::String paramID, caption; };

class XefirusEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    XefirusEditor (juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& apvts, MeteringEngine& engine, AbStateManager& ab,
                   const juce::String& bypassParamID, const std::array<KnobSpec, 6>& knobSpecs)
        : juce::AudioProcessorEditor (proc), top (apvts, bypassParamID, ab, engine), analysis (engine)
    {
        addAndMakeVisible (top);
        for (const auto& k : knobSpecs) { knobs.push_back (std::make_unique<ParameterKnob> (apvts, k.paramID, k.caption, knobLaf)); addAndMakeVisible (*knobs.back()); }
        addAndMakeVisible (analysis);
        analysis.onLayoutChange = [this] { resized(); };
        setResizable (true, true); setResizeLimits (760, 460, 1800, 1200);
        setSize (980, 640);
        startTimerHz (30);
    }
    ~XefirusEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient bgGrad (Theme::bg().brighter (0.06f), 0, 0, Theme::bg(), 0, (float) getHeight(), false);
        g.setGradientFill (bgGrad); g.fillAll();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        top.setBounds (b.removeFromTop (54));
        const int analysisH = analysis.isCollapsed() ? AnalysisZone::kHeaderH + 4 : juce::jmax (240, (int) (b.getHeight() * 0.52f));
        analysis.setBounds (b.removeFromBottom (analysisH).reduced (6, 4));

        auto k = b.reduced (12, 6);
        const int cols = k.getWidth() >= 640 ? 6 : 3, rows = cols == 6 ? 1 : 2;
        const int cw = k.getWidth() / cols, ch = k.getHeight() / rows;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (k.getX() + (int) (i % (size_t) cols) * cw, k.getY() + (int) (i / (size_t) cols) * ch, cw, ch).reduced (4));
    }

private:
    void timerCallback() override { top.tick(); analysis.tick(); }

    KnobLookAndFeel knobLaf;
    TopBar top; AnalysisZone analysis;
    std::vector<std::unique_ptr<ParameterKnob>> knobs;
};
} // namespace xef::ui