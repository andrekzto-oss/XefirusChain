#pragma once
#include "../Metering/XefMeteringEngine.h"
#include "XefTheme.h"
#include <array>

namespace xef::ui
{
class LevelMetersView : public juce::Component
{
public:
    explicit LevelMetersView (MeteringEngine& e) : engine (e) {}
    void mouseDown (const juce::MouseEvent&) override { engine.resetHolds(); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "LEVELS  dBFS   (RMS fill / sample peak / hold)   click = reset");
        auto area = getLocalBounds().toFloat().reduced (8.0f).withTrimmedTop (16.0f);
        auto scale = area.removeFromLeft (24.0f);
        const float top = area.getY() + 10.0f, bottom = area.getBottom() - 14.0f;
        auto yOf = [=] (float db) { return juce::jmap (juce::jlimit (kMin, 0.0f, db), kMin, 0.0f, bottom, top); };

        g.setFont (9.0f);
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            g.setColour (Theme::grid()); g.drawHorizontalLine ((int) yOf (db), scale.getRight(), area.getRight());
            g.setColour (Theme::dim());  g.drawText (juce::String ((int) db), scale.withY (yOf (db) - 6.0f).withHeight (12.0f), juce::Justification::centredRight, false);
        }

        const char* names[4] = { "IN L", "IN R", "OUT L", "OUT R" };
        const float slotW = area.getWidth() / 4.0f;
        for (int b = 0; b < 4; ++b)
        {
            const auto& m = b < 2 ? engine.inLevel : engine.outLevel; const int c = b & 1;
            auto slot = juce::Rectangle<float> (area.getX() + slotW * (float) b, area.getY(), slotW, area.getHeight()).reduced (slotW * 0.16f, 0.0f);
            const float xL = slot.getX(), w = slot.getWidth();

            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (xL, top, w, bottom - top);
            const float rmsY = yOf (m.rmsDb[(size_t) c].load());
            juce::ColourGradient grad (Theme::warn(), 0, top, Theme::accent(), 0, bottom, false);
            grad.addColour (0.18, Theme::ember()); grad.addColour (0.45, Theme::hot());
            g.setGradientFill (grad); g.fillRect (xL, rmsY, w, bottom - rmsY);

            g.setColour (Theme::text()); g.fillRect (xL, yOf (m.peakDb[(size_t) c].load()) - 1.0f, w, 2.0f);
            g.setColour (Theme::ember()); g.fillRect (xL, yOf (m.holdDb[(size_t) c].load()) - 1.0f, w, 1.5f);

            g.setColour (m.clip[(size_t) c].load() ? Theme::warn() : Theme::grid());
            g.fillEllipse (xL + w * 0.5f - 4.0f, area.getY(), 8.0f, 8.0f);
            g.setColour (Theme::dim()); g.setFont (9.0f);
            g.drawText (names[b], juce::Rectangle<float> (xL - 6.0f, bottom + 1.0f, w + 12.0f, 12.0f), juce::Justification::centred, false);
        }
    }

private:
    static constexpr float kMin = -60.0f;
    MeteringEngine& engine;
};

class LoudnessView : public juce::Component
{
public:
    explicit LoudnessView (MeteringEngine& e) : engine (e)
    {
        addAndMakeVisible (resetBtn); resetBtn.setButtonText ("RESET");
        resetBtn.onClick = [this] { engine.resetLoudness(); };
        addAndMakeVisible (target);
        target.setSliderStyle (juce::Slider::LinearHorizontal); target.setRange (-36.0, -6.0, 0.5);
        target.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16); target.setTextValueSuffix (" LUFS");
        target.setValue (engine.settings.lufsTarget.load(), juce::dontSendNotification);
        target.onValueChange = [this] { engine.settings.lufsTarget.store ((float) target.getValue()); };
        target.setColour (juce::Slider::thumbColourId, Theme::hot()); target.setColour (juce::Slider::trackColourId, Theme::accent());
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8);
        resetBtn.setBounds (b.removeFromBottom (18).removeFromRight (56));
        target.setBounds (getLocalBounds().reduced (8).removeFromBottom (18).withTrimmedRight (62));
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "LOUDNESS  BS.1770 / R128");
        const float mo = engine.outLoud.getMomentary(), st = engine.outLoud.getShortTerm(), in = engine.outLoud.getIntegrated(), lra = engine.outLoud.getLra();
        const float tgt = engine.settings.lufsTarget.load();

        auto area = getLocalBounds().reduced (8).withTrimmedTop (16).withTrimmedBottom (22);
        auto bar = area.removeFromRight (16).toFloat();
        auto yOf = [&] (float l) { return juce::jmap (juce::jlimit (-50.0f, -5.0f, l), -50.0f, -5.0f, bar.getBottom(), bar.getY()); };

        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (bar);
        g.setColour (Theme::accent()); g.fillRect (bar.getX(), yOf (mo), bar.getWidth(), bar.getBottom() - yOf (mo));
        g.setColour (Theme::text());  g.fillRect (bar.getX() - 3.0f, yOf (st) - 1.0f, bar.getWidth() + 6.0f, 2.0f);
        g.setColour (Theme::ember()); g.fillRect (bar.getX() - 3.0f, yOf (in) - 1.0f, bar.getWidth() + 6.0f, 2.0f);
        g.setColour (Theme::hot());   g.drawHorizontalLine ((int) yOf (tgt), bar.getX() - 8.0f, bar.getRight() + 3.0f);
        g.setFont (8.0f); g.setColour (Theme::dim());
        for (float l : { -5.0f, -14.0f, -23.0f, -33.0f, -50.0f })
            g.drawText (juce::String ((int) l), (int) bar.getX() - 28, (int) yOf (l) - 5, 18, 10, juce::Justification::centredRight, false);

        const auto rows = area.withTrimmedRight (30);
        const float rh = (float) rows.getHeight() / 5.0f;
        auto row = [&] (int i, const char* name, const juce::String& val, juce::Colour col)
        {
            auto r = juce::Rectangle<float> ((float) rows.getX(), (float) rows.getY() + rh * (float) i, (float) rows.getWidth(), rh);
            g.setColour (Theme::dim());  g.setFont (9.0f);  g.drawText (name, r.removeFromLeft (r.getWidth() * 0.5f), juce::Justification::centredLeft, false);
            g.setColour (col);           g.setFont (15.0f); g.drawText (val, r, juce::Justification::centredRight, false);
        };
        row (0, "MOMENTARY 400ms", fmtDb (mo), Theme::hot());
        row (1, "SHORT-TERM 3s",   fmtDb (st), Theme::text());
        row (2, "INTEGRATED",      fmtDb (in), Theme::ember());
        row (3, "LRA (LU)",        lra > 0.0f ? juce::String (lra, 1) : juce::String ("--"), Theme::text());

        const float ref = in > -70.0f ? in : st;
        const bool have = ref > -70.0f;
        const float diff = ref - tgt;
        row (4, in > -70.0f ? "vs TARGET (INT)" : "vs TARGET (ST)", have ? juce::String (diff > 0 ? "+" : "") + juce::String (diff, 1) + " LU" : juce::String ("--"),
             have && std::fabs (diff) > 1.0f ? Theme::ember() : Theme::accent());
    }

private:
    MeteringEngine& engine; juce::TextButton resetBtn; juce::Slider target;
};

class CorrelationView : public juce::Component
{
public:
    explicit CorrelationView (MeteringEngine& e) : engine (e) { history.fill (0.0f); validHist.fill (false); }

    void tick()
    {
        history[(size_t) pos] = engine.corr.getValue(); validHist[(size_t) pos] = engine.corr.isValid();
        pos = (pos + 1) % (int) history.size();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "STEREO CORRELATION");
        auto b = getLocalBounds().toFloat().reduced (10.0f).withTrimmedTop (16.0f);
        auto bar = b.removeFromTop (18.0f);
        auto xOf = [&] (float v) { return bar.getX() + bar.getWidth() * (v + 1.0f) * 0.5f; };

        auto zone = [&] (float a, float c, juce::Colour col) { g.setColour (col); g.fillRect (xOf (a), bar.getY(), xOf (c) - xOf (a), bar.getHeight()); };
        zone (-1.0f, -0.25f, Theme::warn().withAlpha (0.45f));
        zone (-0.25f, 0.25f, Theme::ember().withAlpha (0.25f));
        zone ( 0.25f, 1.0f,  Theme::accent().withAlpha (0.40f));
        g.setColour (Theme::text()); g.drawRect (bar, 1.0f); g.drawVerticalLine ((int) xOf (0.0f), bar.getY() - 3.0f, bar.getBottom() + 3.0f);
        g.setFont (9.0f); g.setColour (Theme::dim());
        for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            g.drawText (v > 0 ? "+" + juce::String (v, 1) : juce::String (v, 1), (int) xOf (v) - 14, (int) bar.getBottom() + 2, 28, 10, juce::Justification::centred, false);

        const bool ok = engine.corr.isValid(); const float v = engine.corr.getValue();
        if (ok)
        {
            g.setColour (v < -0.25f ? Theme::warn() : Theme::hot());
            g.fillRect (xOf (v) - 2.0f, bar.getY() - 4.0f, 4.0f, bar.getHeight() + 8.0f);
        }
        g.setColour (ok && v < -0.25f ? Theme::warn() : Theme::text()); g.setFont (13.0f);
        g.drawText (ok ? (v > 0 ? "+" : "") + juce::String (v, 2) : juce::String ("--  (signal too low)"), b.removeFromTop (30.0f).withTrimmedTop (12.0f), juce::Justification::centred, false);

        auto h = b.reduced (0.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRect (h);
        g.setColour (Theme::grid()); g.drawHorizontalLine ((int) h.getCentreY(), h.getX(), h.getRight());
        const int n = (int) history.size();
        for (int i = 0; i < n; ++i)
        {
            const int idx = (pos + i) % n; if (! validHist[(size_t) idx]) continue;
            const float x = h.getX() + h.getWidth() * (float) i / (float) n, y = juce::jmap (history[(size_t) idx], -1.0f, 1.0f, h.getBottom(), h.getY());
            g.setColour (history[(size_t) idx] < -0.25f ? Theme::warn() : Theme::accent()); g.fillRect (x, y - 1.0f, juce::jmax (1.0f, h.getWidth() / (float) n), 2.0f);
        }
    }

private:
    MeteringEngine& engine;
    std::array<float, 150> history; std::array<bool, 150> validHist; int pos = 0;
};

class GoniometerView : public juce::Component
{
public:
    explicit GoniometerView (MeteringEngine& e) : engine (e)
    {
        addAndMakeVisible (intensity); intensity.setRange (0.1, 1.0, 0.01); intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); intensity.setValue (engine.settings.gonioIntensity.load(), juce::dontSendNotification);
        intensity.onValueChange = [this] { engine.settings.gonioIntensity.store ((float) intensity.getValue()); };
        intensity.setColour (juce::Slider::thumbColourId, Theme::hot()); intensity.setColour (juce::Slider::trackColourId, Theme::accent());
    }

    void resized() override
    {
        intensity.setBounds (getLocalBounds().reduced (10).removeFromBottom (14));
        const auto s = scopeArea();
        trail = juce::Image (juce::Image::RGB, juce::jmax (1, (int) s.getWidth()), juce::jmax (1, (int) s.getHeight()), true);
    }

    void tick()
    {
        if (! trail.isValid()) return;
        float tmp[4096];
        size_t avail = engine.scope.ring.available() & ~(size_t) 1;
        if (avail > 2400) { engine.scope.ring.skip (avail - 2400); avail = 2400; }
        const size_t got = engine.scope.ring.read (tmp, avail) & ~(size_t) 1;

        juce::Graphics g (trail);
        g.setColour (juce::Colours::black.withAlpha (0.22f)); g.fillAll();
        const float cx = (float) trail.getWidth() * 0.5f, cy = (float) trail.getHeight() * 0.5f, R = juce::jmin (cx, cy) * 0.95f;
        const float a = 0.15f + 0.85f * engine.settings.gonioIntensity.load();
        g.setColour (Theme::hot().withAlpha (a));
        for (size_t i = 0; i + 1 < got; i += 2)
        {
            const float l = tmp[i], r = tmp[i + 1];
            g.fillRect (cx + (r - l) * 0.5f * R - 0.75f, cy - (l + r) * 0.5f * R - 0.75f, 1.5f, 1.5f);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "VECTORSCOPE  M/S");
        const auto s = scopeArea();
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (s, 3.0f);
        if (trail.isValid()) g.drawImageAt (trail, (int) s.getX(), (int) s.getY());
        const auto c = s.getCentre(); const float R = juce::jmin (s.getWidth(), s.getHeight()) * 0.5f * 0.95f;
        g.setColour (Theme::grid().brighter (0.2f)); g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.0f);
        g.drawLine (c.x, c.y - R, c.x, c.y + R); g.drawLine (c.x - R, c.y, c.x + R, c.y);
        g.drawLine (c.x - R * 0.7071f, c.y - R * 0.7071f, c.x + R * 0.7071f, c.y + R * 0.7071f);
        g.drawLine (c.x - R * 0.7071f, c.y + R * 0.7071f, c.x + R * 0.7071f, c.y - R * 0.7071f);
        g.setColour (Theme::dim()); g.setFont (9.0f);
        g.drawText ("M", (int) c.x - 10, (int) (c.y - R) - 1, 20, 10, juce::Justification::centred, false);
        g.drawText ("L", (int) (c.x - R * 0.7071f) - 12, (int) (c.y - R * 0.7071f) - 6, 12, 10, juce::Justification::centred, false);
        g.drawText ("R", (int) (c.x + R * 0.7071f), (int) (c.y - R * 0.7071f) - 6, 12, 10, juce::Justification::centred, false);
        g.drawText ("S", (int) (c.x + R) - 10, (int) c.y - 12, 10, 10, juce::Justification::centred, false);
    }

private:
    juce::Rectangle<float> scopeArea() const
    {
        auto b = getLocalBounds().toFloat().reduced (10.0f).withTrimmedTop (14.0f).withTrimmedBottom (18.0f);
        const float side = juce::jmin (b.getWidth(), b.getHeight());
        return juce::Rectangle<float> (side, side).withCentre (b.getCentre());
    }
    MeteringEngine& engine; juce::Slider intensity; juce::Image trail;
};

class GainReductionView : public juce::Component
{
public:
    explicit GainReductionView (MeteringEngine& e) : engine (e) {}
    void mouseDown (const juce::MouseEvent&) override { engine.resetHolds(); }

    void tick()
    {
        const int rc = engine.gr.resetCounter.load();
        if (rc != lastReset) { lastReset = rc; hold.fill (0.0f); }
        const float v[3] = { engine.gr.compDb.load(), maxBand(), engine.gr.limiterDb.load() };
        for (int i = 0; i < 3; ++i) { disp[(size_t) i] = juce::jmax (v[i], disp[(size_t) i] - 0.7f); hold[(size_t) i] = juce::jmax (hold[(size_t) i], v[i]); }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "GAIN REDUCTION  dB   click = reset hold");
        auto area = getLocalBounds().toFloat().reduced (10.0f).withTrimmedTop (16.0f);
        const char* names[3] = { "COMP", "MULTIBAND", "LIMITER" };
        const float rh = area.getHeight() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto r = juce::Rectangle<float> (area.getX(), area.getY() + rh * (float) i, area.getWidth(), rh).reduced (0.0f, 3.0f);
            g.setColour (Theme::dim()); g.setFont (9.0f); g.drawText (names[i], r.removeFromLeft (58.0f), juce::Justification::centredLeft, false);
            auto val = r.removeFromRight (54.0f);
            const bool na = (i == 1 && engine.gr.numBands.load() == 0);
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (r);
            auto xOf = [&] (float db) { return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, db / kMaxGr); };
            if (! na)
            {
                g.setColour (i == 2 ? Theme::ember() : Theme::accent()); g.fillRect (r.getX(), r.getY(), xOf (disp[(size_t) i]) - r.getX(), r.getHeight());
                g.setColour (Theme::text()); g.fillRect (xOf (hold[(size_t) i]) - 1.0f, r.getY(), 2.0f, r.getHeight());
            }
            g.setColour (Theme::text()); g.setFont (11.0f);
            g.drawText (na ? juce::String ("n/a") : "-" + juce::String (disp[(size_t) i], 1) + " / " + juce::String (hold[(size_t) i], 1), val, juce::Justification::centredRight, false);
        }
    }

private:
    float maxBand() const { float m = 0; for (int b = 0; b < engine.gr.numBands.load(); ++b) m = juce::jmax (m, engine.gr.bandDb[(size_t) b].load()); return m; }
    static constexpr float kMaxGr = 18.0f;
    MeteringEngine& engine; std::array<float, 3> disp {}, hold {}; int lastReset = 0;
};
} // namespace xef::ui