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

#include "PluginEditor.hpp"

#include "BinaryData.h"

PluginEditor::PluginEditor(PluginProcessor& p) :
    AudioProcessorEditor(&p), p_ref_(p), property_(initProperty(p)), base_(p.state_), main_panel_(p, base_),
    chat_panel_(p, base_), sensoryPanel(p) {
    // set font
#if defined(JUCE_WINDOWS)
    base_.font_ = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSubsetMediumNoHinting_ttf,
                                                          BinaryData::InterSubsetMediumNoHinting_ttfSize);
#else
    base_.font_ = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSubsetMedium_ttf,
                                                          BinaryData::InterSubsetMedium_ttfSize);
#endif
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(base_.font_);
    // add the main panel
    addAndMakeVisible(main_panel_);
    addAndMakeVisible(chat_panel_);
    main_panel_.getControlPanel().addMouseListener(this, true);
    main_panel_.getOutputPanel().addMouseListener(this, true);

    // set size & size listener
    setResizeLimits(static_cast<int>(zlstate::PWindowW::kMinV - 1), static_cast<int>(zlstate::PWindowH::kMinV - 1),
                    static_cast<int>(zlstate::PWindowW::kMaxV + 1), static_cast<int>(zlstate::PWindowH::kMaxV + 1));
    setResizable(true, p.wrapperType != PluginProcessor::wrapperType_AudioUnitv3);

    this->resizableCorner = std::make_unique<zlgui::ResizeCorner>(base_, this, getConstrainer(),
                                                                  zlgui::ResizeCorner::kScaleWithFontSize, 1.25f);
    addChildComponent(this->resizableCorner.get());
    this->resizableCorner->setAlwaysOnTop(true);
    this->resizableCorner->resized();

    last_ui_width_.referTo(p.state_.getParameterAsValue(zlstate::PWindowW::kID));
    last_ui_height_.referTo(p.state_.getParameterAsValue(zlstate::PWindowH::kID));

    // Ensure minimum window size to accommodate chat panel
    int initialWidth = last_ui_width_.getValue();
    int initialHeight = last_ui_height_.getValue();
    const int minWidth = 1150;
    const int minHeight = 600;

    if (initialWidth < minWidth || initialHeight < minHeight) {
        initialWidth = std::max(initialWidth, minWidth);
        initialHeight = std::max(initialHeight, minHeight);
        last_ui_width_ = initialWidth;
        last_ui_height_ = initialHeight;
    }
    addAndMakeVisible(sensoryPanel);
    setSize(initialWidth, initialHeight);

    startTimerHz(1);
    updateIsShowing();

    base_.setPanelProperty(zlgui::kUISettingChanged, true);
    base_.getPanelValueTree().addListener(this);
    // =========================================================
    // [LingoEQ Integration] 桥接：后端 AI 操作完成 -> 前端触发 UI 联动
    // 注意：因为我们在 AgentClient 收到消息时已经用了 MessageManager::callAsync
    // 所以这里的闭包绝对运行在安全的 UI 主线程中！
    // =========================================================
    p_ref_.onAIBandAdjusted = [this](const std::vector<int>& touchedBands) {
        // 必须切回主线程
        juce::MessageManager::callAsync([this, touchedBands]() {
            // 1. 如果列表里有多个点 (Batch / Preset)
            if (touchedBands.size() > 1) {
                // 触发全屏重绘，保证曲线瞬间刷新
                repaint();

                // 【核心需求实现】只遍历 Python 传过来的那些有效点
                for (int bandIdx : touchedBands) {
                    // 让这些特定的点闪烁
                    main_panel_.triggerAIFlash(static_cast<size_t>(bandIdx));
                }

                // 批量操作后，将焦点归位到列表里的第一个点，或者归 0
                if (!touchedBands.empty()) {
                    base_.setSelectedBand(static_cast<size_t>(touchedBands[0]));
                }
            }
            // 2. 如果列表里只有一个点 (Single Adjustment)
            else if (touchedBands.size() == 1) {
                int bandIdx = touchedBands[0];
                size_t idx = static_cast<size_t>(bandIdx);

                base_.setSelectedBand(idx);
                main_panel_.triggerAIFlash(idx);

                // 单点通常不需要全屏 repaint，setSelectedBand 内部通常会处理
                // 如果需要也可以加 repaint();
            }
        });
    };
}

PluginEditor::~PluginEditor() {
    p_ref_.onAIBandAdjusted = nullptr;
    base_.getPanelValueTree().removeListener(this);
    vblank_.reset();
    stopTimer();
    p_ref_.getController().setEditorON(false);
}

void PluginEditor::paint(juce::Graphics& g) { juce::ignoreUnused(g); }

void PluginEditor::resized() {
    // [LingoEQ Integration]
    auto area = getLocalBounds();

    // 350 for ChatPanel
    chat_panel_.setBounds(area.removeFromRight(300));

    // left for MainPanel
    main_panel_.setBounds(area);

    //  limit
    last_ui_width_ =
        std::clamp(getWidth(), static_cast<int>(zlstate::PWindowW::kMinV), static_cast<int>(zlstate::PWindowW::kMaxV));
    last_ui_height_ =
        std::clamp(getHeight(), static_cast<int>(zlstate::PWindowH::kMinV), static_cast<int>(zlstate::PWindowH::kMaxV));
    triggerAsyncUpdate();

    sensoryPanel.setBounds(getWidth() - 420, 20, 100, 100);
}

void PluginEditor::visibilityChanged() { updateIsShowing(); }

void PluginEditor::parentHierarchyChanged() { updateIsShowing(); }

void PluginEditor::minimisationStateChanged(bool) { updateIsShowing(); }

void PluginEditor::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& property) {
    if (base_.isPanelIdentifier(zlgui::kUISettingChanged, property)) {
        sendLookAndFeelChange();
        auto& fft{p_ref_.getController().getFFTAnalyzer()};
        fft.setExtraTilt(base_.getFFTExtraTilt());
        fft.setExtraSpeed(base_.getFFTExtraSpeed());
        triggerAsyncUpdate();
    }
}

void PluginEditor::handleAsyncUpdate() { property_.saveAPVTS(p_ref_.state_); }

void PluginEditor::timerCallback() { updateIsShowing(); }

void PluginEditor::updateIsShowing() {
    const auto is_showing = isShowing();
    if (is_showing != base_.getIsEditorShowing()) {
        base_.setIsEditorShowing(is_showing);
        p_ref_.getController().setEditorON(is_showing);
        if (is_showing) {
            main_panel_.startThreads();
            vblank_ = std::make_unique<juce::VBlankAttachment>(
                &main_panel_, [this](const double x) { main_panel_.repaintCallBack(x); });
        } else {
            vblank_.reset();
            main_panel_.stopThreads();
        }
    }
}

int PluginEditor::getControlParameterIndex(Component& c) {
    const auto id = c.getComponentID();
    if (id.isEmpty()) {
        return -1;
    }
    if (const auto para = p_ref_.parameters_.getParameter(id); para == nullptr) {
        return -1;
    } else {
        return para->getParameterIndex();
    }
}

void PluginEditor::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isRightButtonDown() && event.getNumberOfClicks() == 1) {
        if (event.originalComponent != nullptr) {
            if (const auto id = event.originalComponent->getComponentID(); !id.isEmpty()) {
                if (const auto para = p_ref_.parameters_.getParameter(id); para != nullptr) {
                    if (const auto* context = getHostContext(); context != nullptr) {
                        if (auto menu = context->getContextMenuForParameter(para)) {
                            menu->showNativeMenu(juce::Component::getMouseXYRelative());
                        }
                    }
                }
            }
        }
    }
}

zlstate::Property& PluginEditor::initProperty(PluginProcessor& p) {
    p.property_.loadAPVTS(p.state_);
    return p.property_;
}