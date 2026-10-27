#pragma once
#include "../Metering/XefMeteringEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace xef
{
class AbStateManager : private juce::Timer
{
public:
    AbStateManager (juce::AudioProcessorValueTreeState& s, MeteringEngine& e) : apvts (s), engine (e) {}
    ~AbStateManager() override { stopTimer(); }

    int  getActive() const noexcept { return active; }
    bool isSwitching() const noexcept { return pending; }
    std::function<void()> onChanged;

    void toggle()
    {
        if (pending) return;
        slots[active] = apvts.copyState();
        if (! slots[1 - active].isValid()) slots[1 - active] = slots[active].createCopy();
        target = 1 - active; waited = 0; pending = true;
        engine.duckAck.store (false); engine.duckRequested.store (true);
        startTimer (5);
    }

    void copyActiveToOther()
    {
        slots[active] = apvts.copyState();
        slots[1 - active] = slots[active].createCopy();
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree v ("XefAB"); v.setProperty ("active", active, nullptr);
        for (int i = 0; i < 2; ++i) if (slots[i].isValid()) { auto c = slots[i].createCopy(); v.addChild (c, -1, nullptr); c.setProperty ("xefSlot", i, nullptr); }
        return v;
    }
    void fromValueTree (const juce::ValueTree& v)
    {
        if (! v.isValid()) return;
        active = (int) v.getProperty ("active", 0) ? 1 : 0;
        for (auto c : v) { const int i = (int) c.getProperty ("xefSlot", 0); if (i == 0 || i == 1) slots[i] = c.createCopy(); }
    }

private:
    void timerCallback() override
    {
        waited += 5;
        if (! engine.duckAck.load() && waited < 250) return;
        stopTimer();
        if (slots[target].isValid()) apvts.replaceState (slots[target].createCopy());
        active = target; pending = false;
        engine.duckRequested.store (false);
        if (onChanged) onChanged();
    }

    juce::AudioProcessorValueTreeState& apvts;
    MeteringEngine& engine;
    juce::ValueTree slots[2];
    int active = 0, target = 0, waited = 0; bool pending = false;
};
} // namespace xef