#pragma once

#include <array>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI.h"

class ScorchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ScorchEditor(ScorchProcessor&);
    ~ScorchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    ScorchProcessor& proc;

    ui::Knob drive   { "DRIVE", true };
    ui::Knob bias    { "BIAS" };
    ui::Knob fold    { "FOLD" };
    ui::Knob crush   { "CRUSH" };
    ui::Knob decim   { "DECIMATE" };
    ui::Knob punch   { "PUNCH" };
    ui::Knob split   { "SPLIT" };
    ui::Knob sub     { "SUB" };
    ui::Knob lowcut  { "LOW CUT" };
    ui::Knob highcut { "HIGH CUT" };
    ui::Knob ceiling { "CEILING" };
    ui::Knob width   { "WIDTH" };
    ui::Knob mix     { "MIX" };
    ui::Knob out     { "OUTPUT" };

    ui::CurveView curve { proc };
    ui::PresetBar presets { proc };
    ui::MeterBar meter;
    ui::Segmented mode;
    ui::Segmented os;
    ui::Segmented monoSub;

    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    float dispL = 0.0f, dispR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScorchEditor)
};
