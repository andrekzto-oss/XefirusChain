#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "XefEditor.h"

class PluginEditor : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(XefirusChainProcessor& p);
    ~PluginEditor() override;
    void resized() override;
    void paint(juce::Graphics& g) override {}
private:
    XefirusChainProcessor& audioProcessor;
    std::unique_ptr<xef::ui::XefirusEditor> xefEditor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};