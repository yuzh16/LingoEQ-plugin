// Copyright (C) 2025 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General
// Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see
// <https://www.gnu.org/licenses/>.

#pragma once

#include "PluginProcessor.hpp"

#include "gui/gui.hpp"
#include "panel/chat_panel/chat_panel.hpp"
#include "panel/main_panel.hpp"
#include "state/state.hpp"

   class SensoryPanel : public juce::Component, public juce::Timer {
public:
    SensoryPanel(PluginProcessor& p) : processor(p) { startTimerHz(30); }

    ~SensoryPanel() override { stopTimer(); }

    void timerCallback() override {
        float newProgress = processor.sensoryCaptureProgress.load();

        // 检查 5 个槽位的状态是否有变化
        bool statusChanged = false;
        for (int i = 0; i < PluginProcessor::kSensorySlotNum; ++i) {
            bool isOccupied = processor.sensorySlotStatus[i].load();
            if (isOccupied != currentSlotStatus[i]) {
                currentSlotStatus[i] = isOccupied;
                statusChanged = true;
            }
        }

        if (newProgress != currentProgress || statusChanged) {
            currentProgress = newProgress;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        auto bounds = getLocalBounds().toFloat();
        float buttonRadius = 25.0f;
        auto buttonArea = bounds.withSizeKeepingCentre(buttonRadius * 2, buttonRadius * 2).translated(0, -10.0f);

        // 1. 如果点击了中央大按钮 (REC)
        if (buttonArea.contains(e.position)) {
            if (currentProgress < 0.0f) {
                processor.triggerSensoryCapture();
            }
            return; // 点击了 REC 就直接返回
        }

        // 2. 如果点击了下方的小圆点 (触发删除)
        float dotRadius = 4.0f;
        float spacing = 12.0f;
        float totalWidth = (5 - 1) * spacing;
        float startX = bounds.getCentreX() - totalWidth / 2.0f;
        float dotY = buttonArea.getBottom() + 15.0f;

        for (int i = 0; i < 5; ++i) {
            // 放宽点击判定区域 (Hitbox)，半径设为 8.0f，让用户更容易点中
            juce::Rectangle<float> hitBox(startX + i * spacing - 8.0f, dotY - 8.0f, 16.0f, 16.0f);

            if (hitBox.contains(e.position)) {
                // 如果该槽位有数据，则触发删除
                if (currentSlotStatus[i]) {
                    processor.deleteSensorySlot(i);
                }
                return;
            }
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float buttonRadius = 25.0f;
        auto buttonArea = bounds.withSizeKeepingCentre(buttonRadius * 2, buttonRadius * 2).translated(0, -10.0f);

        // 画中心按钮
        g.setColour(currentProgress >= 0.0f ? juce::Colours::red.withAlpha(0.8f) : juce::Colour(0xFF333333));
        g.fillEllipse(buttonArea);

        g.setColour(juce::Colours::white);
        g.drawText(currentProgress >= 0.0f ? "REC" : "AI EAR", buttonArea, juce::Justification::centred);

        // 画进度环
        if (currentProgress >= 0.0f) {
            juce::Path progressPath;
            float endAngle = juce::MathConstants<float>::twoPi * currentProgress;
            progressPath.addCentredArc(buttonArea.getCentreX(), buttonArea.getCentreY(), buttonRadius + 4.0f,
                                       buttonRadius + 4.0f, 0.0f, 0.0f, endAngle, true);
            g.setColour(juce::Colours::cyan);
            g.strokePath(progressPath, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved));
        }

        // 画下方的 5 个记忆槽位
        float dotRadius = 4.0f;
        float spacing = 12.0f;
        float totalWidth = (5 - 1) * spacing;
        float startX = bounds.getCentreX() - totalWidth / 2.0f;
        float dotY = buttonArea.getBottom() + 15.0f;

        for (int i = 0; i < 5; ++i) {
            juce::Rectangle<float> dotArea(startX + i * spacing - dotRadius, dotY - dotRadius, dotRadius * 2,
                                           dotRadius * 2);

            // 读取数组状态而不是总数
            if (currentSlotStatus[i]) {
                g.setColour(juce::Colours::cyan);
                g.fillEllipse(dotArea);
                g.setColour(juce::Colours::cyan.withAlpha(0.3f));
                g.fillEllipse(dotArea.expanded(2.0f));
            } else {
                g.setColour(juce::Colours::grey.withAlpha(0.5f));
                g.drawEllipse(dotArea, 1.0f);
            }
        }
    }

private:
    PluginProcessor& processor;
    float currentProgress = -1.0f;
    // 缓存 UI 状态，用于对比是否需要重绘
    std::array<bool, 5> currentSlotStatus{false, false, false, false, false};
};

//==============================================================================
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater {
public:
    explicit PluginEditor(PluginProcessor& p);

    ~PluginEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;

    void resized() override;

    void visibilityChanged() override;

    void parentHierarchyChanged() override;

    void minimisationStateChanged(bool isNowMinimised) override;

    int getControlParameterIndex(Component& c) override;

    void mouseDown(const juce::MouseEvent& event) override;

    SensoryPanel sensoryPanel;

private:
    PluginProcessor& p_ref_;
    zlstate::Property& property_;
    juce::Value last_ui_width_, last_ui_height_;

    zlgui::UIBase base_;
    zlpanel::MainPanel main_panel_;

    zlpanel::ChatPanel chat_panel_;

    std::unique_ptr<juce::VBlankAttachment> vblank_;

    void timerCallback() override;

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& property) override;

    void handleAsyncUpdate() override;

    void updateIsShowing();

    static zlstate::Property& initProperty(PluginProcessor& p);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

