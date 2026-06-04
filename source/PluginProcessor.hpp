// Copyright (C) 2025 - zsliu98
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectralAnalyzer.hpp"
#include "zlp/zlp.hpp"
#include "state/state.hpp"
#include "AgentClient.hpp"

class PluginProcessor final : public juce::AudioProcessor,
                              public juce::AudioProcessorValueTreeState::Listener,
                              public juce::Timer {
public:
    static constexpr int kSensorySlotNum = 5;

    zlstate::DummyProcessor dummy_processor_;
    juce::AudioProcessorValueTreeState parameters_;
    juce::AudioProcessorValueTreeState parameters_NA_;
    juce::AudioProcessorValueTreeState state_;
    zlstate::Property property_;
    AgentClient agentClient;

    void timerCallback() override;
    void triggerSensoryCapture();
    // -1.0 表示空闲，0.0 ~ 1.0 表示采样进度
    std::atomic<float> sensoryCaptureProgress{-1.0f};

    // 记录 5 个槽位的使用状态：true=已占用，false=空闲
    std::array<std::atomic<bool>, kSensorySlotNum> sensorySlotStatus{};

    // 供 UI 调用的精确删除方法
    void deleteSensorySlot(int slotIndex);

    PluginProcessor();
    ~PluginProcessor() override;

    void processToolCommand(const juce::var& json);

    void prepareToPlay(double sample_rate, int samples_per_block) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override;

    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    std::function<void(const std::vector<int>&)> onAIBandAdjusted;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override;
    void getStateInformation(juce::MemoryBlock& dest_data) override;
    void setStateInformation(const void* data, int size_in_bytes) override;

    bool supportsDoublePrecisionProcessing() const override { return true; }

    zlp::Controller& getController() { return controller_; }

    double getAtomicSampleRate() const { return sample_rate_.load(std::memory_order::relaxed); }

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    std::atomic<bool> autoCheckEnabled{true};
  

private:
    std::array<std::vector<double>, 2> main_buffer_, side_buffer_;
    std::array<double*, 2> main_pointers_{}, side_pointers_{};
    zlp::Controller controller_;
    std::array<std::unique_ptr<zlp::FilterAttach>, zlp::kBandNum> filter_attachments_;
    std::array<std::unique_ptr<zlp::FilterDynamicAttach>, zlp::kBandNum> filter_dynamic_attachments_;
    std::array<std::unique_ptr<zlp::FilterSideAttach>, zlp::kBandNum> filter_side_attachments_;
    zlp::ChoreAttach chore_attachment_;
    zlp::AnalyzerAttach analyzer_attachment_;

    std::atomic<bool> isUpdatingFromAI{false};

    void updateSingleBandFromJSON(int bandIdx, const juce::var& args);
    void resetAllBands();

    SpectralAnalyzer analyzer;
    bool isCapturing = false;
    // =======================================================
    // [Shadow Recorder] 影子录音机基建
    // =======================================================
    enum class ShadowState { Normal, CollectingPostAction };
    std::atomic<ShadowState> shadowState{ShadowState::Normal};

    juce::Array<juce::var> shadowRingBuffer; // 永远保存最近1秒的有效声音
    juce::Array<juce::var> preActionBuffer; // 锁存的操作前数据
    juce::Array<juce::var> postActionBuffer; // 收集的操作后数据
    juce::Array<juce::var> shadowFreqs; // 对比时的频率点
    int postActionCount = 0;

    // 音频流检测器（用于判断 DAW 是否暂停）
    std::atomic<int> processedSamplesSinceLastFrame{0};

    // 获取当前的 24 个频段频率
    std::vector<float> getCurrentBandFreqs();

    // 触发影子对比
    void triggerShadowReflexion();
    // =======================================================
    int captureCount = 0;
    juce::Array<juce::var> capturedFrames;
    std::vector<float> currentFreqs;

    enum ChannelLayout { kMain1Aux0, kMain1Aux1, kMain1Aux2, kMain2Aux0, kMain2Aux1, kMain2Aux2, kInvalid };

    std::atomic<float>&ext_side_, &bypass_;
    ChannelLayout channel_layout_{kInvalid};

    std::atomic<double> sample_rate_{48000.0};

    template <bool bypass = false>
    void processBlockInternal(juce::AudioBuffer<float>& buffer);

    template <bool bypass = false>
    void processBlockInternal(juce::AudioBuffer<double>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
