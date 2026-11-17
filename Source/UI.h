// Scorch - minimalist UI components (header only).
#pragma once

#include <algorithm>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace ui
{
inline const juce::Colour bg     { 0xff0c0c0e };
inline const juce::Colour panel  { 0xff131316 };
inline const juce::Colour line   { 0xff26262b };
inline const juce::Colour dim    { 0xff70707a };
inline const juce::Colour text   { 0xffdcdce0 };
inline const juce::Colour accent { 0xffff5a3c };

inline juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
// Rotary knob: thin arc, pointer, name below (value while hovering / dragging).
class Knob : public juce::Slider
{
public:
    explicit Knob(const juce::String& labelText, bool isBig = false)
        : label(labelText), big(isBig)
    {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                            juce::MathConstants<float>::pi * 2.75f, true);
        setMouseDragSensitivity(220);
        setScrollWheelEnabled(true);
    }

    void mouseEnter(const juce::MouseEvent& e) override { juce::Slider::mouseEnter(e); repaint(); }
    void mouseExit(const juce::MouseEvent& e) override  { juce::Slider::mouseExit(e); repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto labelArea = area.removeFromBottom(big ? 24.0f : 20.0f);
        const float d = std::min(area.getWidth(), area.getHeight()) - (big ? 20.0f : 12.0f);
        const auto c = area.getCentre();
        const float r = d * 0.5f;
        const float thick = big ? 5.0f : 3.0f;

        const float a0 = juce::MathConstants<float>::pi * 1.25f;
        const float a1 = juce::MathConstants<float>::pi * 2.75f;
        const float prop = (float) valueToProportionOfLength(getValue());
        const float ang = a0 + prop * (a1 - a0);
        const bool bipolar = getMinimum() < 0.0 && getMaximum() > 0.0;
        const bool hot = isMouseOverOrDragging();

        const juce::PathStrokeType stroke(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc(c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour(line);
        g.strokePath(track, stroke);

        const float from = bipolar ? a0 + (float) valueToProportionOfLength(0.0) * (a1 - a0) : a0;
        if (std::abs(ang - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc(c.x, c.y, r, r, 0.0f, std::min(from, ang), std::max(from, ang), true);
            g.setColour(accent.withAlpha(hot ? 1.0f : 0.85f));
            g.strokePath(value, stroke);
        }

        const float p0 = r * (big ? 0.58f : 0.40f), p1 = r * (big ? 0.84f : 0.72f);
        g.setColour(hot ? text : dim.brighter(0.4f));
        g.drawLine(juce::Line<float>(c.x + p0 * std::sin(ang), c.y - p0 * std::cos(ang),
                                     c.x + p1 * std::sin(ang), c.y - p1 * std::cos(ang)),
                   big ? 2.5f : 2.0f);

        if (big)
        {
            g.setColour(text);
            g.setFont(font(21.0f, true));
            g.drawText(getTextFromValue(getValue()),
                       juce::Rectangle<float>(c.x - r * 0.62f, c.y - 14.0f, r * 1.24f, 28.0f),
                       juce::Justification::centred, false);
        }

        g.setFont(font(big ? 12.0f : 11.0f, big));
        g.setColour(hot ? text : dim);
        g.drawText((hot && ! big) ? getTextFromValue(getValue()) : label, labelArea,
                   juce::Justification::centred, false);
    }

private:
    juce::String label;
    bool big;
};

//==============================================================================
// Row of text choices bound to a choice / bool parameter.
class Segmented : public juce::Component
{
public:
    Segmented(juce::RangedAudioParameter& param, juce::StringArray names)
        : labels(std::move(names)),
          attachment(param, [this](float v) { selected = juce::roundToInt(v); repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(panel);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(line);
        g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);

        const int n = labels.size();
        if (n == 0) return;
        const float w = b.getWidth() / (float) n;
        g.setFont(font(11.0f, true));
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float>(b.getX() + w * (float) i, b.getY(), w, b.getHeight());
            const bool on = (i == selected);
            if (on)
            {
                g.setColour(accent.withAlpha(0.16f));
                g.fillRoundedRectangle(cell.reduced(3.0f), 6.0f);
            }
            g.setColour(on ? accent : dim);
            g.drawText(labels[i], cell, juce::Justification::centred, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int n = labels.size();
        if (n == 0 || getWidth() <= 0) return;
        const int idx = juce::jlimit(0, n - 1, (int) (e.position.x / ((float) getWidth() / (float) n)));
        attachment.setValueAsCompleteGesture((float) idx);
    }

private:
    juce::StringArray labels;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// Live static transfer curve of the shaper chain (drive, mode, bias, fold, crush, ceiling).
class CurveView : public juce::Component
{
public:
    explicit CurveView(ScorchProcessor& p) : proc(p) {}

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(panel);
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(line);
        g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);

        const auto a = b.reduced(18.0f);
        auto X = [&a](float x) { return a.getX() + (x * 0.5f + 0.5f) * a.getWidth(); };
        auto Y = [&a](float y) { return a.getBottom() - (y * 0.5f + 0.5f) * a.getHeight(); };

        // grid
        g.setColour(line);
        for (int i = -1; i <= 1; ++i)
        {
            const float v = 0.5f * (float) i;
            g.drawLine(X(v * 2.0f), a.getY(), X(v * 2.0f), a.getBottom(), i == 0 ? 1.2f : 0.6f);
            g.drawLine(a.getX(), Y(v * 2.0f), a.getRight(), Y(v * 2.0f), i == 0 ? 1.2f : 0.6f);
        }
        g.drawLine(X(-1.0f), Y(-1.0f), X(1.0f), Y(1.0f), 0.8f); // unity reference

        const bd::Params prm = proc.readParams();
        const bd::Shape shape = bd::makeShape(prm);

        // ceiling guides
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour(dim.withAlpha(0.45f));
        g.drawDashedLine(juce::Line<float>(a.getX(), Y(shape.ceil), a.getRight(), Y(shape.ceil)), dashes, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(a.getX(), Y(-shape.ceil), a.getRight(), Y(-shape.ceil)), dashes, 2, 1.0f);

        juce::Path path;
        constexpr int points = 240;
        for (int i = 0; i <= points; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) points;
            const float y = juce::jlimit(-1.0f, 1.0f, bd::curveSample(shape, x));
            if (i == 0) path.startNewSubPath(X(x), Y(y));
            else        path.lineTo(X(x), Y(y));
        }

        g.setColour(accent.withAlpha(0.16f));
        g.strokePath(path, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(dim);
        g.setFont(font(10.0f));
        g.drawText("TRANSFER", b.reduced(12.0f, 8.0f), juce::Justification::topLeft, false);
        g.drawText("IN", b.reduced(12.0f, 8.0f), juce::Justification::bottomRight, false);
    }

private:
    ScorchProcessor& proc;
};

//==============================================================================
// Preset selector: < name >, click the name for a menu.
class PresetBar : public juce::Component
{
public:
    explicit PresetBar(ScorchProcessor& p) : proc(p) {}

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(panel);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(line);
        g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);

        g.setColour(dim);
        g.setFont(font(15.0f, true));
        g.drawText("<", b.removeFromLeft(34.0f), juce::Justification::centred, false);
        g.drawText(">", b.removeFromRight(34.0f), juce::Justification::centred, false);

        g.setColour(text);
        g.setFont(font(13.0f, true));
        g.drawText(proc.getProgramName(proc.getCurrentProgram()), b, juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int count = proc.getNumPrograms();
        const int cur = proc.getCurrentProgram();
        if (e.x < 34)                  { proc.setCurrentProgram((cur + count - 1) % count); repaint(); }
        else if (e.x > getWidth() - 34) { proc.setCurrentProgram((cur + 1) % count); repaint(); }
        else
        {
            juce::PopupMenu menu;
            for (int i = 0; i < count; ++i)
                menu.addItem(i + 1, proc.getProgramName(i), true, i == cur);

            juce::Component::SafePointer<PresetBar> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                               [safe](int result)
                               {
                                   if (safe != nullptr && result > 0)
                                   {
                                       safe->proc.setCurrentProgram(result - 1);
                                       safe->repaint();
                                   }
                               });
        }
    }

private:
    ScorchProcessor& proc;
};

//==============================================================================
// Thin stereo output meter.
class MeterBar : public juce::Component
{
public:
    void setLevels(float l, float r)
    {
        left = l; right = r;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float h = b.getHeight() * 0.5f - 1.0f;
        draw(g, b.removeFromTop(h), left);
        b.removeFromTop(2.0f);
        draw(g, b, right);
    }

private:
    static void draw(juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        const float db = 20.0f * std::log10(std::max(level, 1.0e-5f));
        const float prop = juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
        g.setColour(line);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (prop > 0.0f)
        {
            g.setColour(db > -0.5f ? juce::Colour(0xffff2a2a) : accent);
            g.fillRoundedRectangle(r.withWidth(std::max(r.getHeight(), r.getWidth() * prop)), r.getHeight() * 0.5f);
        }
    }

    float left = 0.0f, right = 0.0f;
};

} // namespace ui
