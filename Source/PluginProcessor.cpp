#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ScorchProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&layout](const char* id, const char* name, NormalisableRange<float> range, float def,
                          std::function<String(float, int)> fmt)
    {
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withStringFromValueFunction(std::move(fmt))));
    };

    auto db  = [](float v, int) { return String(v, 1) + " dB"; };
    auto sdb = [](float v, int) { return String(v > 0.05f ? "+" : "") + String(v, 1) + " dB"; };
    auto pct = [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; };
    auto hz  = [](float v, int)
    {
        return v >= 1000.0f ? String(v / 1000.0f, 1) + " kHz" : String(roundToInt(v)) + " Hz";
    };

    addF("drive",   "Drive",    { 0.0f, 48.0f, 0.01f },        12.0f,  db);
    addF("bias",    "Bias",     { -1.0f, 1.0f, 0.001f },       0.0f,   pct);
    addF("fold",    "Fold",     { 0.0f, 1.0f, 0.001f },        0.0f,   pct);
    addF("crush",   "Crush",    { 0.0f, 1.0f, 0.001f },        0.0f,   pct);
    addF("decim",   "Decimate", { 0.0f, 1.0f, 0.001f },        0.0f,   pct);
    addF("punch",   "Punch",    { -12.0f, 12.0f, 0.01f },      0.0f,   sdb);
    addF("split",   "Split",    { 40.0f, 300.0f, 0.0f, 0.5f }, 120.0f, hz);
    addF("sub",     "Sub",      { 0.0f, 1.0f, 0.001f },        0.5f,   pct);
    addF("lowcut",  "Low Cut",  { 20.0f, 500.0f, 0.0f, 0.4f }, 20.0f,  hz);
    addF("highcut", "High Cut", { 1000.0f, 20000.0f, 0.0f, 0.35f }, 20000.0f, hz);
    addF("ceiling", "Ceiling",  { -24.0f, 0.0f, 0.01f },       -1.0f,  db);
    addF("width",   "Width",    { 0.0f, 2.0f, 0.001f },        1.0f,   pct);
    addF("mix",     "Mix",      { 0.0f, 1.0f, 0.001f },        1.0f,   pct);
    addF("out",     "Output",   { -24.0f, 12.0f, 0.01f },      0.0f,   sdb);

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { "mode", 1 }, "Mode", StringArray { "Tube", "Fuzz", "Fold", "Hard", "Rage" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID { "os", 1 }, "Oversampling", StringArray { "Off", "2x", "4x" }, 1));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "monosub", 1 }, "Mono Sub", true));

    return layout;
}

//==============================================================================
ScorchProcessor::ScorchProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
}

bd::Params ScorchProcessor::readParams() const
{
    auto g = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    bd::Params p;
    p.driveDb   = g("drive");
    p.mode      = juce::jlimit(0, (int) bd::NumModes - 1, juce::roundToInt(g("mode")));
    p.bias      = g("bias");
    p.fold      = g("fold");
    p.crush     = g("crush");
    p.decimate  = g("decim");
    p.punchDb   = g("punch");
    p.splitHz   = g("split");
    p.sub       = g("sub");
    p.monoSub   = g("monosub") > 0.5f;
    p.lowCutHz  = g("lowcut");
    p.highCutHz = g("highcut");
    p.ceilDb    = g("ceiling");
    p.width     = g("width");
    p.mix       = g("mix");
    p.outDb     = g("out");
    p.os        = juce::jlimit(0, 2, juce::roundToInt(g("os")));
    return p;
}

//==============================================================================
void ScorchProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    engine.setParams(readParams());
    setLatencySamples(engine.latency());
}

bool ScorchProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ScorchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear(c, 0, n);

    if (buffer.getNumChannels() < 2)
        return;

    engine.setParams(readParams());
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);

    if (engine.latency() != getLatencySamples())
        setLatencySamples(engine.latency());

    float l = 0.0f, r = 0.0f;
    engine.takePeaks(l, r);
    if (l > meterL.load()) meterL.store(l);
    if (r > meterR.load()) meterR.store(r);
}

//==============================================================================
void ScorchProcessor::setCurrentProgram(int index)
{
    const auto& list = bd::presets();
    if (index < 0 || index >= (int) list.size())
        return;

    currentProgram = index;
    const bd::Params& p = list[(size_t) index].p;

    auto set = [this](const char* id, float plainValue)
    {
        if (auto* prm = apvts.getParameter(id))
            prm->setValueNotifyingHost(prm->convertTo0to1(plainValue));
    };

    set("drive", p.driveDb);   set("mode", (float) p.mode);  set("bias", p.bias);
    set("fold", p.fold);       set("crush", p.crush);        set("decim", p.decimate);
    set("punch", p.punchDb);   set("split", p.splitHz);      set("sub", p.sub);
    set("monosub", p.monoSub ? 1.0f : 0.0f);
    set("lowcut", p.lowCutHz); set("highcut", p.highCutHz);  set("ceiling", p.ceilDb);
    set("width", p.width);     set("mix", p.mix);            set("out", p.outDb);
    set("os", (float) p.os);
}

const juce::String ScorchProcessor::getProgramName(int index)
{
    const auto& list = bd::presets();
    if (index < 0 || index >= (int) list.size())
        return {};
    return list[(size_t) index].name;
}

//==============================================================================
void ScorchProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("program", currentProgram, nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ScorchProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            currentProgram = juce::jlimit(0, getNumPrograms() - 1, (int) apvts.state.getProperty("program", 0));
        }
}

juce::AudioProcessorEditor* ScorchProcessor::createEditor()
{
    return new ScorchEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScorchProcessor();
}
