#include "PluginProcessor.h"
#include "PluginEditor.h"

XefirusChainProcessor::XefirusChainProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters()) { ab = std::make_unique<xef::AbStateManager>(apvts, engine); }
XefirusChainProcessor::~XefirusChainProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout XefirusChainProcessor::createParameters() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("bypass", 1), "Bypass", false));
    const char* ids[] = { "demon", "tone", "punch", "width", "heat", "loudness" };
    const char* names[] = { "DEMON", "TONE", "PUNCH", "WIDTH", "HEAT", "LOUDNESS" };
    for (int i = 0; i < 6; ++i) params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(ids[i], 1), names[i], 0.0f, 10.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clipper_ceiling", 1), "CEILING", -24.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clipper_gain", 1), "GAIN", -12.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("clipper_mode", 1), "SOFT/HARD", false));
    return { params.begin(), params.end() };
}

void XefirusChainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate);
    outStage.prepare(sampleRate, samplesPerBlock, getLatencySamples());
}
bool XefirusChainProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

void XefirusChainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(); float* l = buffer.getWritePointer(0); float* r = buffer.getWritePointer(1);
    engine.measureInput(l, r, n); outStage.pushDry(l, r, n);
    engine.gr.reportCompressor(1.0f);
    engine.measureWet(l, r, n);
    const bool bypass = apvts.getRawParameterValue("bypass")->load() > 0.5f;
    const bool ducked = outStage.apply(l, r, n, bypass, engine.duckRequested.load(), engine.updateMatchGainDb(n));
    engine.duckAck.store(ducked);
    engine.measureOutput(l, r, n);
    outStage.applyMonoMonitor(l, r, n, engine.settings.monoMonitor.load());
}

juce::AudioProcessorEditor* XefirusChainProcessor::createEditor() { return new PluginEditor(*this); }

void XefirusChainProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState(); state.addChild(ab->toValueTree(), -1, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml()); copyXmlToBinary(*xml, destData);
}
void XefirusChainProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr) if (xmlState->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState)); ab->fromValueTree(apvts.state.getChildWithName("XefAB"));
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XefirusChainProcessor(); }