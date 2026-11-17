#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassDrive.h"
#include "Presets.h"

class ScorchProcessor : public juce::AudioProcessor
{
public:
    ScorchProcessor();
    ~ScorchProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scorch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return (int) bd::presets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // snapshot of all parameters in plain units (used by DSP and by the UI curve)
    bd::Params readParams() const;

    // peak levels since the last call (linear), for the UI meter
    void takeMeters(float& l, float& r) { l = meterL.exchange(0.0f); r = meterR.exchange(0.0f); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    bd::Engine engine;
    int currentProgram = 0;
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScorchProcessor)
};
