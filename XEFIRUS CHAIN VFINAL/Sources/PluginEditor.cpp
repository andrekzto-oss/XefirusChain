#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginEditor::PluginEditor(XefirusChainProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p) {
    const std::array<xef::ui::KnobSpec, 6> specs = {{
        { "demon", "DEMON" }, { "tone", "TONE" }, { "punch", "PUNCH" }, { "width", "WIDTH" }, { "heat", "HEAT" }, { "loudness", "LOUDNESS" }
    }};
    xefEditor = std::make_unique<xef::ui::XefirusEditor>(p, p.apvts, p.engine, *p.ab, "bypass", specs);
    addAndMakeVisible(xefEditor.get());
    setSize(980, 640); setResizable(true, true); setResizeLimits(760, 460, 1800, 1200);
}
PluginEditor::~PluginEditor() {}
void PluginEditor::resized() { if (xefEditor != nullptr) xefEditor->setBounds(getLocalBounds()); }