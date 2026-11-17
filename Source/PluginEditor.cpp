#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 860, kHeight = 516;
constexpr int kColX = 568, kCellW = 89, kCellH = 86, kRowPitch = 98, kRowY = 94;
}

ScorchEditor::ScorchEditor(ScorchProcessor& p)
    : juce::AudioProcessorEditor(&p),
      proc(p),
      mode(*p.apvts.getParameter("mode"), juce::StringArray { "TUBE", "FUZZ", "FOLD", "HARD", "RAGE" }),
      os(*p.apvts.getParameter("os"), juce::StringArray { "OFF", "2x", "4x" }),
      monoSub(*p.apvts.getParameter("monosub"), juce::StringArray { "LOW STEREO", "LOW MONO" })
{
    const std::array<std::pair<ui::Knob*, const char*>, 14> knobMap { {
        { &drive, "drive" },   { &bias, "bias" },     { &fold, "fold" },     { &crush, "crush" },
        { &decim, "decim" },   { &punch, "punch" },   { &split, "split" },   { &sub, "sub" },
        { &lowcut, "lowcut" }, { &highcut, "highcut" }, { &ceiling, "ceiling" }, { &width, "width" },
        { &mix, "mix" },       { &out, "out" } } };

    for (const auto& entry : knobMap)
    {
        addAndMakeVisible(*entry.first);
        attachments.push_back(std::make_unique<SliderAttachment>(proc.apvts, entry.second, *entry.first));
    }

    addAndMakeVisible(curve);
    addAndMakeVisible(presets);
    addAndMakeVisible(meter);
    addAndMakeVisible(mode);
    addAndMakeVisible(os);
    addAndMakeVisible(monoSub);

    setSize(kWidth, kHeight);
    setResizable(false, false);
    startTimerHz(30);
}

ScorchEditor::~ScorchEditor()
{
    stopTimer();
}

void ScorchEditor::timerCallback()
{
    float l = 0.0f, r = 0.0f;
    proc.takeMeters(l, r);
    dispL = std::max(l, dispL * 0.86f);
    dispR = std::max(r, dispR * 0.86f);
    meter.setLevels(dispL, dispR);
    curve.repaint();
    presets.repaint();
}

void ScorchEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::bg);

    // wordmark
    g.setColour(ui::accent);
    g.fillEllipse(24.0f, 22.0f, 8.0f, 8.0f);
    g.setColour(ui::text);
    g.setFont(ui::font(22.0f, true));
    g.drawText("SCORCH", juce::Rectangle<int>(40, 14, 160, 26), juce::Justification::centredLeft, false);
    g.setColour(ui::dim);
    g.setFont(ui::font(10.0f));
    g.drawText("BASS DISTORTION", juce::Rectangle<int>(24, 40, 180, 14), juce::Justification::centredLeft, false);

    g.drawText("OVERSAMPLING", juce::Rectangle<int>(kWidth - 24 - 132 - 96, 20, 90, 26),
               juce::Justification::centredRight, false);

    g.setColour(ui::line);
    g.drawLine(24.0f, 66.0f, (float) kWidth - 24.0f, 66.0f, 1.0f);

    // section titles above each knob row
    g.setColour(ui::dim);
    g.setFont(ui::font(10.0f, true));
    const char* titles[] = { "SHAPE", "LOW END", "TONE", "OUTPUT" };
    for (int i = 0; i < 4; ++i)
        g.drawText(titles[i], juce::Rectangle<int>(kColX, kRowY - 15 + i * kRowPitch, 200, 12),
                   juce::Justification::centredLeft, false);
}

void ScorchEditor::resized()
{
    presets.setBounds((kWidth - 280) / 2, 18, 280, 30);
    os.setBounds(kWidth - 24 - 132, 20, 132, 26);

    // left: transfer curve + mode + low band routing
    curve.setBounds(24, 84, 280, 280);
    mode.setBounds(24, 378, 280, 32);
    monoSub.setBounds(24, 420, 280, 32);

    // centre: drive + bias
    drive.setBounds(324, 84, 216, 236);
    bias.setBounds(324 + (216 - 96) / 2, 326, 96, 92);

    // right: 4 rows x 3 knobs
    ui::Knob* rows[4][3] = {
        { &fold, &crush, &decim },
        { &punch, &split, &sub },
        { &lowcut, &highcut, &ceiling },
        { &width, &mix, &out }
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            rows[r][c]->setBounds(kColX + c * kCellW, kRowY + r * kRowPitch, kCellW, kCellH);

    meter.setBounds(24, kHeight - 26, kWidth - 48, 8);
}
