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

#include "PluginProcessor.hpp"
#include "PluginEditor.hpp"

// ==========================================================
// [LingoEQ] 绝对标尺：标准 1/3 倍频程 24 段测试网格
// 注意：这是一个独立的全局静态变量，不属于 PluginProcessor 类
// ==========================================================
static const std::vector<float> kStandardSpectrumGrid = {
    31.5f,  40.0f,  50.0f,  63.0f,   80.0f,   100.0f,  125.0f,  160.0f,  200.0f,  250.0f,  315.0f,   400.0f,
    500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 12500.0f, 16000.0f};

//==============================================================================
PluginProcessor::PluginProcessor() :
    AudioProcessor(BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withInput("Aux", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
dummy_processor_(), parameters_(*this, nullptr, juce::Identifier("ZLEqualizerParameters"), zlp::getParameterLayout()),
parameters_NA_(dummy_processor_, nullptr, juce::Identifier("ZLEqualizerNAParameters"), zlstate::getNAParameterLayout()),
state_(dummy_processor_, nullptr, juce::Identifier("ZLEqualizerState"), zlstate::getStateParameterLayout()),
property_(state_), controller_(*this), chore_attachment_(*this, parameters_, controller_),
analyzer_attachment_(*this, parameters_NA_, controller_),
ext_side_(*parameters_.getRawParameterValue(zlp::PExtSide::kID)),
bypass_(*parameters_.getRawParameterValue(zlp::PBypass::kID)) {
    for (size_t i = 0; i < zlp::kBandNum; ++i) {
        filter_attachments_[i] = std::make_unique<zlp::FilterAttach>(*this, parameters_, controller_, i);
        filter_dynamic_attachments_[i] = std::make_unique<zlp::FilterDynamicAttach>(*this, parameters_, controller_, i);
        filter_side_attachments_[i] = std::make_unique<zlp::FilterSideAttach>(*this, parameters_, controller_, i);
    }

    // ============================================================
    // [LingoEQ Integration] 注册参数监听器
    // ============================================================

    // 遍历所有频段，监听我们关心的核心参数
    for (size_t i = 0; i < zlp::kBandNum; ++i) {
        juce::String idx = juce::String(i);

        // 监听: 频率、增益、Q值
        parameters_.addParameterListener("freq" + idx, this);
        parameters_.addParameterListener("gain" + idx, this);
        parameters_.addParameterListener("q" + idx, this);

        // 监听: 滤波器类型
        parameters_.addParameterListener("filter_type" + idx, this);

        // 监听: 开关状态 (Enabled/Bypass)
        // 注意：ZLEqualizer 的参数名可能叫 filter_status 或 bypass
        // 根据之前的代码分析，它是 "filter_status" + idx
        parameters_.addParameterListener("filter_status" + idx, this);

        // 监听: 动态 EQ 开关
        // 注意：根据之前的分析，它是 "dynamic_on" + idx
        parameters_.addParameterListener("dynamic_on" + idx, this);

        // 监听: 动态阈值
        parameters_.addParameterListener("threshold" + idx, this);
    }

    // 监听: 全局参数 (可选)
    parameters_.addParameterListener("total_output_gain", this);
    parameters_.addParameterListener("total_bypass", this);

    // ============================================================

    agentClient.onToolCallReceived = [this](const juce::var& json) {
        // 使用 MessageManager 确保在主线程执行参数修改，避免多线程竞争
        juce::MessageManager::callAsync([this, json]() { this->processToolCommand(json); });
    };

    // 启动连接
    agentClient.connectToServer("127.0.0.1", 8888);
    // [新增] 启动全局巡航定时器，每 50ms 一次
    startTimer(50);
}

PluginProcessor::~PluginProcessor() {

    for (size_t i = 0; i < zlp::kBandNum; ++i) {
        juce::String idx = juce::String(i);
        parameters_.removeParameterListener("freq" + idx, this);
        parameters_.removeParameterListener("gain" + idx, this);
        parameters_.removeParameterListener("q" + idx, this);
        parameters_.removeParameterListener("filter_type" + idx, this);
        parameters_.removeParameterListener("filter_status" + idx, this);
        parameters_.removeParameterListener("dynamic_on" + idx, this);
        parameters_.removeParameterListener("threshold" + idx, this);
    }

    parameters_.removeParameterListener("total_output_gain", this);
    parameters_.removeParameterListener("total_bypass", this);

    // [LingoEQ Integration]
    // 插件销毁时，必须强制停止网络线程。
    // 如果不手动 stop，由于 AgentClient 是个独立线程，它可能还在后台跑，
    // 而宿主已经把内存回收了，这会导致严重的 Crash。
    agentClient.stop();
}

// ==============================================================================
// [LingoEQ Core] 2. 核心指令执行逻辑 (The Brain-Hand Connection)
// ==============================================================================
// PluginProcessor.cpp

void PluginProcessor::processToolCommand(const juce::var& json) {
    // ⚠️ 关键：加锁，防止修改参数时触发 listener 导致回传给 Python
    isUpdatingFromAI = true;

    // 1. 基础校验
    if (!json.isObject()) {
        isUpdatingFromAI = false; // 退出前解锁
        return;
    }

    const juce::String toolName = json["tool"].toString();
    const juce::var args = json["args"];

    // ========================================================
    // A. 单频段调节 (adjust_eq_band)
    // ========================================================
    if (toolName == "adjust_eq_band") {
        int bandIdx = args.getProperty("band_index", 0);

        // 调用我们刚刚写的辅助函数
        updateSingleBandFromJSON(bandIdx, args);

        // 通知 UI 播放“旋钮转动”动画
        if (onAIBandAdjusted) {
            onAIBandAdjusted({bandIdx});
        }
    }
    // ========================================================
    // B. [NEW] 批量预设加载 (adjust_eq_batch)
    // ========================================================
    else if (toolName == "adjust_eq_batch") {
        resetAllBands();
        if (args.hasProperty("bands") && args["bands"].isArray()) {
            auto bandsArray = args["bands"];
            int numBands = bandsArray.size();

            // 1. 创建一个列表，用来记录本次“有效”的频段索引
            std::vector<int> touchedBands;

            for (int i = 0; i < numBands; ++i) {
                const juce::var& bandData = bandsArray[i];
                if (bandData.hasProperty("band_index")) {
                    int idx = static_cast<int>(bandData["band_index"]);
                    updateSingleBandFromJSON(idx, bandData);

                    // 【关键】把修改过的索引记下来
                    touchedBands.push_back(idx);
                }
            }

            // 2. 将这个列表传给 UI
            if (!touchedBands.empty() && onAIBandAdjusted) {
                onAIBandAdjusted(touchedBands);
            }
        }
    }
    // ========================================================
    // C. 全局控制 (set_global_control)
    // ========================================================
    else if (toolName == "set_global_control") {
        if (args.hasProperty("output_gain")) {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameters_.getParameter("total_output_gain"))) {
                float val = static_cast<float>(args["output_gain"]);
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(val));
                p->endChangeGesture();
            }
        }
        if (args.hasProperty("bypass")) {
            if (auto* p = parameters_.getParameter("total_bypass")) {
                float val = static_cast<bool>(args["bypass"]) ? 1.0f : 0.0f;
                p->beginChangeGesture();
                p->setValueNotifyingHost(val);
                p->endChangeGesture();
            }
        }
    }

    // 👉 【修改】如果是调整 EQ 的指令，且【开启了自检模式】，才触发影子对比锁定！
    if (autoCheckEnabled.load()) {
        if (toolName == "adjust_eq_band" || toolName == "adjust_eq_batch") {
            juce::MessageManager::callAsync([this]() { triggerShadowReflexion(); });
        }
    }

    // ⚠️ 关键：操作全部完成后解锁
    isUpdatingFromAI = false;
}


//==============================================================================
const juce::String PluginProcessor::getName() const { return JucePlugin_Name; }

bool PluginProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

int PluginProcessor::getNumPrograms() {
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram() { return 0; }

void PluginProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }

const juce::String PluginProcessor::getProgramName(int index) {
    juce::ignoreUnused(index);
    return {};
}

void PluginProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void PluginProcessor::prepareToPlay(const double sample_rate, const int samples_per_block) {
    for (size_t i = 0; i < 2; ++i) {
        main_buffer_[i].resize(static_cast<size_t>(samples_per_block));
        main_pointers_[i] = main_buffer_[i].data();
        side_buffer_[i].resize(static_cast<size_t>(samples_per_block));
        side_pointers_[i] = side_buffer_[i].data();
    }
    // determine current channel layout
    const auto* main_bus = getBus(true, 0);
    const auto* aux_bus = getBus(true, 1);
    channel_layout_ = ChannelLayout::kInvalid;
    if (main_bus == nullptr) {
        return;
    }
    if (main_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
        if (aux_bus == nullptr || !aux_bus->isEnabled()) {
            channel_layout_ = ChannelLayout::kMain1Aux0;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
            channel_layout_ = ChannelLayout::kMain1Aux1;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
            channel_layout_ = ChannelLayout::kMain1Aux2;
        }
    } else if (main_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
        if (aux_bus == nullptr || !aux_bus->isEnabled()) {
            channel_layout_ = ChannelLayout::kMain2Aux0;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
            channel_layout_ = ChannelLayout::kMain2Aux1;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
            channel_layout_ = ChannelLayout::kMain2Aux2;
        }
    }
    controller_.prepare(sample_rate, static_cast<size_t>(samples_per_block));
    sample_rate_.store(sample_rate, std::memory_order::relaxed);
}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
        (layouts.getChannelSet(true, 1).isDisabled() ||
         layouts.getChannelSet(true, 1) == juce::AudioChannelSet::mono() ||
         layouts.getChannelSet(true, 1) == juce::AudioChannelSet::stereo())) {
        return true;
    }
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono() &&
        (layouts.getChannelSet(true, 1).isDisabled() ||
         layouts.getChannelSet(true, 1) == juce::AudioChannelSet::mono() ||
         layouts.getChannelSet(true, 1) == juce::AudioChannelSet::stereo())) {
        return true;
    }
    return false;
}

// 1. 处理 float (32位) 的版本
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // 原有的处理逻辑
    if (bypass_.load(std::memory_order::relaxed) > .5f) {
        processBlockInternal<true>(buffer);
    } else {
        processBlockInternal<false>(buffer);
    }
}

// 2. 处理 double (64位) 的版本 (Cubase 走这里)
void PluginProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages) {
    // 原有的处理逻辑
    if (bypass_.load(std::memory_order::relaxed) > .5f) {
        processBlockInternal<true>(buffer);
    } else {
        processBlockInternal<false>(buffer);
    }
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processBlockInternal<true>(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) {
    processBlockInternal<true>(buffer);
}

template <bool bypass>
void PluginProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer) {
    juce::ScopedNoDenormals no_denormals;
    if (buffer.getNumSamples() == 0) {
        return; // ignore empty blocks
    }
    const auto c_ext_side = ext_side_.load(std::memory_order::relaxed) > .5f;
    const auto num_samples = static_cast<size_t>(buffer.getNumSamples());
    switch (channel_layout_) {
    case kMain1Aux0: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        break;
    }
    case kMain1Aux1: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(1), num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        }
        zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        break;
    }
    case kMain1Aux2: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(1), num_samples);
            zldsp::vector::copy(side_pointers_[1], buffer.getReadPointer(2), num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], main_pointers_[0], num_samples);
        }

        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        break;
    }
    case kMain2Aux0: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], buffer.getReadPointer(1), num_samples);
        zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        zldsp::vector::copy(buffer.getWritePointer(1), main_pointers_[1], num_samples);
        break;
    }
    case kMain2Aux1: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], buffer.getReadPointer(1), num_samples);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(2), num_samples);
            zldsp::vector::copy(side_pointers_[1], buffer.getReadPointer(2), num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        }
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        zldsp::vector::copy(buffer.getWritePointer(1), main_pointers_[1], num_samples);
        break;
    }
    case kMain2Aux2: {
        zldsp::vector::copy(main_pointers_[0], buffer.getReadPointer(0), num_samples);
        zldsp::vector::copy(main_pointers_[1], buffer.getReadPointer(1), num_samples);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(2), num_samples);
            zldsp::vector::copy(side_pointers_[1], buffer.getReadPointer(3), num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        }
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        zldsp::vector::copy(buffer.getWritePointer(0), main_pointers_[0], num_samples);
        zldsp::vector::copy(buffer.getWritePointer(1), main_pointers_[1], num_samples);
        break;
    }
    case kInvalid: {
        return;
    }
    }
    // ==========================================
    // [感知层] 获取立体声混合 (Mono Mixdown) 的输出音频
    // ==========================================
    int numChannels = buffer.getNumChannels();
    if (numChannels == 1) {
        // 单声道直接喂入
        auto* channelData = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            analyzer.pushSample(channelData[i]);
        }
    } else if (numChannels >= 2) {
        // 立体声：左右声道平均 (L + R) * 0.5
        auto* channelL = buffer.getReadPointer(0);
        auto* channelR = buffer.getReadPointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            analyzer.pushSample((channelL[i] + channelR[i]) * 0.5f);
        }
    }

    // [新增] 告诉系统：音频正在流动！没有暂停！
    processedSamplesSinceLastFrame.fetch_add(buffer.getNumSamples(), std::memory_order_relaxed);
}

template <bool bypass>
void PluginProcessor::processBlockInternal(juce::AudioBuffer<double>& buffer) {
    juce::ScopedNoDenormals no_denormals;
    if (buffer.getNumSamples() == 0) {
        return; // ignore empty blocks
    }
    const auto c_ext_side = ext_side_.load(std::memory_order::relaxed) > .5f;
    const auto num_samples = static_cast<size_t>(buffer.getNumSamples());
    switch (channel_layout_) {
    case kMain1Aux0: {
        main_pointers_[0] = buffer.getWritePointer(0);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kMain1Aux1: {
        main_pointers_[0] = buffer.getWritePointer(0);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        if (c_ext_side) {
            side_pointers_[0] = buffer.getWritePointer(1);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        }
        zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kMain1Aux2: {
        main_pointers_[0] = buffer.getWritePointer(0);
        zldsp::vector::copy(main_pointers_[1], main_pointers_[0], num_samples);
        if (c_ext_side) {
            side_pointers_[0] = buffer.getWritePointer(1);
            side_pointers_[1] = buffer.getWritePointer(2);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        }
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kMain2Aux0: {
        main_pointers_[0] = buffer.getWritePointer(0);
        main_pointers_[1] = buffer.getWritePointer(1);
        zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
        zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kMain2Aux1: {
        main_pointers_[0] = buffer.getWritePointer(0);
        main_pointers_[1] = buffer.getWritePointer(1);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(2), num_samples);
            zldsp::vector::copy(side_pointers_[1], side_pointers_[0], num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        }
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kMain2Aux2: {
        main_pointers_[0] = buffer.getWritePointer(0);
        main_pointers_[1] = buffer.getWritePointer(1);
        if (c_ext_side) {
            zldsp::vector::copy(side_pointers_[0], buffer.getReadPointer(2), num_samples);
            zldsp::vector::copy(side_pointers_[1], buffer.getReadPointer(3), num_samples);
        } else {
            zldsp::vector::copy(side_pointers_[0], main_pointers_[0], num_samples);
            zldsp::vector::copy(side_pointers_[1], main_pointers_[1], num_samples);
        }
        controller_.template process<bypass>(main_pointers_, side_pointers_, num_samples);
        break;
    }
    case kInvalid: {
        return;
    }
    }
    // ==========================================
    // [感知层] 获取立体声混合 (Mono Mixdown) 的输出音频 (Double 精度转 Float)
    // ==========================================
    int numChannels = buffer.getNumChannels();
    if (numChannels == 1) {
        auto* channelData = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            analyzer.pushSample(static_cast<float>(channelData[i]));
        }
    } else if (numChannels >= 2) {
        auto* channelL = buffer.getReadPointer(0);
        auto* channelR = buffer.getReadPointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            analyzer.pushSample(static_cast<float>((channelL[i] + channelR[i]) * 0.5));
        }
    }

    // [新增] 告诉系统：音频正在流动！没有暂停！
    processedSamplesSinceLastFrame.fetch_add(buffer.getNumSamples(), std::memory_order_relaxed);
}

bool PluginProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
    // return new juce::GenericAudioProcessorEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
    auto temp_tree = juce::ValueTree("ZLCompressorParaState");
    temp_tree.appendChild(parameters_.copyState(), nullptr);
    temp_tree.appendChild(parameters_NA_.copyState(), nullptr);
    const std::unique_ptr<juce::XmlElement> xml(temp_tree.createXml());
    copyXmlToBinary(*xml, dest_data);
}

void PluginProcessor::setStateInformation(const void* data, const int size_in_bytes) {
    std::unique_ptr<juce::XmlElement> xml_state(getXmlFromBinary(data, size_in_bytes));
    if (xml_state != nullptr && xml_state->hasTagName("ZLCompressorParaState")) {
        const auto temp_tree = juce::ValueTree::fromXml(*xml_state);
        parameters_.replaceState(temp_tree.getChildWithName(parameters_.state.getType()));
        parameters_NA_.replaceState(temp_tree.getChildWithName(parameters_NA_.state.getType()));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE

createPluginFilter() {
    return new PluginProcessor();
}

// [LingoEQ] 核心重构：将单频段更新逻辑剥离，供 单次调节 和 批量预设 共同调用
void PluginProcessor::updateSingleBandFromJSON(int bandIdx, const juce::var& args) {
    // 1. 滤波器类型映射表 (保持与之前完全一致)
    static const std::map<juce::String, int> filterTypeMap = {
        {"Peaking", 0}, {"LowShelf", 1}, {"LowCut", 4}, {"HighShelf", 3}, {"HighCut", 2}};

    // 2. 定义参数更新 Lambda (复用原逻辑)
    auto updateParam = [&](juce::String jsonKey, juce::String paramPrefix, bool isChoice = false) {
        if (args.hasProperty(jsonKey)) {
            juce::String paramID = paramPrefix + juce::String(bandIdx);

            if (auto* param = parameters_.getParameter(paramID)) {
                // --- 分支 1: 处理下拉菜单 (Filter Type) ---
                if (isChoice) {
                    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                        juce::String typeStr = args.getProperty(jsonKey, "Peaking").toString();

                        // 查表
                        auto it = filterTypeMap.find(typeStr);
                        int targetIndex = (it != filterTypeMap.end()) ? it->second : 0;

                        float normValue = choiceParam->convertTo0to1(targetIndex);
                        choiceParam->beginChangeGesture();
                        choiceParam->setValueNotifyingHost(normValue);
                        choiceParam->endChangeGesture();
                    }
                }
                // --- 分支 2: 处理普通数值 (Freq, Gain, Q) ---
                else if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                    float rawValue = static_cast<float>(args.getProperty(juce::Identifier(jsonKey), 0.0f));
                    float normValue = rangedParam->convertTo0to1(rawValue);

                    rangedParam->beginChangeGesture();
                    rangedParam->setValueNotifyingHost(normValue);
                    rangedParam->endChangeGesture();
                }
            }
        }
    };

    // 3. 执行参数映射
    updateParam("freq", "freq");
    updateParam("gain", "gain");
    updateParam("q", "q");
    updateParam("filter_type", "filter_type", true);
    updateParam("threshold", "threshold");
    updateParam("attack", "attack");
    updateParam("release", "release");

    // 4. 处理特殊开关 (dynamic_enabled -> dynamic_on)
    if (args.hasProperty("dynamic_enabled")) {
        juce::String id = "dynamic_on" + juce::String(bandIdx);
        if (auto* p = parameters_.getParameter(id)) {
            float val = static_cast<bool>(args["dynamic_enabled"]) ? 1.0f : 0.0f;
            p->beginChangeGesture();
            p->setValueNotifyingHost(val);
            p->endChangeGesture();
        }
    }

    // 5. 处理 Enabled 开关
    if (args.hasProperty("enabled")) {
        juce::String id = "filter_status" + juce::String(bandIdx);
        if (auto* p = parameters_.getParameter(id)) {
            bool wantEnable = static_cast<bool>(args["enabled"]);
            float val = wantEnable ? 1.0f : 0.0f;
            p->beginChangeGesture();
            p->setValueNotifyingHost(val);
            p->endChangeGesture();
        }
    }
}

void PluginProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    DBG("Param Changed: " + parameterID + " -> " + juce::String(newValue));
    // ⚠️ 注意：此函数可能在音频线程或消息线程被调用。
    // AgentClient::sendSyncParameter 内部只是 socket.write，通常是安全的。
    // 如果想要极致安全，可以用 MessageManager::callAsync 包裹，但可能会有轻微延迟。
    // 这里我们直接发送。
    if (isUpdatingFromAI) {
        return;
    }

    int bandIdx = -1;
    juce::String paramName;
    juce::var syncValue = newValue; // 默认直接发送数值

    // 辅助 Lambda：解析 ID (例如 "freq5" -> name="freq", idx=5)
    auto parseID = [&](const juce::String& prefix, const juce::String& mappedName) -> bool {
        if (parameterID.startsWith(prefix)) {
            juce::String suffix = parameterID.substring(prefix.length());
            // 简单检查后缀是否为数字
            if (suffix.containsOnly("0123456789")) {
                bandIdx = suffix.getIntValue();
                paramName = mappedName;
                return true;
            }
        }
        return false;
    };

    // --- 开始匹配 ---
    if (parseID("freq", "freq")) {
    } else if (parseID("gain", "gain")) {
    } else if (parseID("q", "q")) {
    }

    // 特殊处理：滤波器类型 (Int -> String)
    else if (parseID("filter_type", "type")) {
        // Python 期望字符串 "Peaking", "LowShelf" 等
        // 映射表必须与 processToolCommand 中的逻辑一致 (0->Peak, 1->LowShelf...)
        static const juce::String types[] = {"Peaking", "LowShelf", "HighCut", "HighShelf", "LowCut"};
        int typeIdx = (int)(newValue + 0.1f); // 简单的 float 转 int
        if (typeIdx >= 0 && typeIdx < 5) {
            syncValue = types[typeIdx];
        }
    }

    // 特殊处理：Enabled 开关 (1.0 -> True)
    else if (parseID("filter_status", "enabled")) {
        syncValue = (newValue > 0.5f);
    }

    // 特殊处理：动态开关
    else if (parseID("dynamic_on", "dynamic_enabled")) {
        syncValue = (newValue > 0.5f);
    }

    else if (parseID("threshold", "threshold")) {
    }

    // --- 发送同步 ---
    if (bandIdx != -1 && paramName.isNotEmpty()) {
        agentClient.sendSyncParameter(bandIdx, paramName, syncValue);
    }
}

void PluginProcessor::resetAllBands() {
    for (int i = 0; i < 24; ++i) {
        // 1. 归零增益 (Gain -> 0.0)
        if (auto* p = parameters_.getParameter("gain" + juce::String(i) )) {
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(p)) {
                floatParam->beginChangeGesture();
                // 确保设置为 0.0 dB (默认值)
                floatParam->setValueNotifyingHost(floatParam->convertTo0to1(0.0f));
                floatParam->endChangeGesture();
            }
        }

        // 2. 关闭频段 (Enabled -> False)
        if (auto* p = parameters_.getParameter("band_" + juce::String(i) + "_enabled")) {
            // 无论是 Bool 还是 Float 参数，设为 0.0f 即为 False
            p->beginChangeGesture();
            p->setValueNotifyingHost(0.0f);
            p->endChangeGesture();
        }
    }
}

// 点击 UI 按钮时触发
void PluginProcessor::triggerSensoryCapture() {
    if (isCapturing)
        return;

    capturedFrames.clear();
    currentFreqs = getCurrentBandFreqs();
    captureCount = 0;
    sensoryCaptureProgress.store(0.0f);

    isCapturing = true; // 标志位开启后，下一次 timerCallback 就会开始收集
}

void PluginProcessor::deleteSensorySlot(int slotIndex) {
    if (slotIndex >= 0 && slotIndex < kSensorySlotNum) {
        // 1. 本地状态标记为空
        sensorySlotStatus[slotIndex].store(false);

        // 2. 通知 Python 后端清空对应的记忆槽
        agentClient.sendDeleteSnapshot(slotIndex);

        DBG("Deleted Sensory Slot: " + juce::String(slotIndex));
    }
}

// 定时器回调：全局巡航，每 50ms 触发
void PluginProcessor::timerCallback() {
    // 1. 【宿主暂停检测】如果这 50ms 内没有任何音频流过，直接跳过，防止录制底噪
    if (processedSamplesSinceLastFrame.exchange(0) < 512) {
        return;
    }

    // 2. 获取当前帧
    auto frame = analyzer.getBinnedSpectrum(kStandardSpectrumGrid, getSampleRate());
    juce::Array<juce::var> frameVar;

    // 【静音检测】找出这帧的最大能量。-80dB 在 0~1 的归一化中是 0.2
    bool isSilence = true;
    for (float val : frame) {
        frameVar.add(val);
        if (val > 0.2f)
            isSilence = false;
    }

    // ==========================================
    // 轨道 A：用户前台 UI 采集 (0~4 槽位)
    // ==========================================
    if (isCapturing) {
        capturedFrames.add(frameVar);
        captureCount++;
        sensoryCaptureProgress.store(captureCount / 20.0f);

        if (captureCount >= 20) {
            isCapturing = false;

            juce::Array<juce::var> freqList;
            for (float f : kStandardSpectrumGrid) {
                freqList.add(f);
            }

            int targetSlot = -1;
            for (int i = 0; i < kSensorySlotNum; ++i) {
                if (!sensorySlotStatus[i].load()) {
                    targetSlot = i;
                    break;
                }
            }

            if (targetSlot != -1) {
                sensorySlotStatus[targetSlot].store(true);
                agentClient.sendSnapshot(capturedFrames, freqList, targetSlot);
            }
            sensoryCaptureProgress.store(-1.0f);
        }
    }

    // ==========================================
    // 轨道 B：后台影子录音机 (A/B 对比机制)
    // ==========================================
    if (!isSilence) {
        // 如果是有效声音，进入状态机
        ShadowState currentState = shadowState.load();

        if (currentState == ShadowState::Normal) {
            // 正常巡航：维护一个 20 帧的环形缓冲区
            shadowRingBuffer.add(frameVar);
            if (shadowRingBuffer.size() > 20) {
                shadowRingBuffer.remove(0); // 挤掉最老的，永远保持最新的 1 秒
            }
        } else if (currentState == ShadowState::CollectingPostAction) {
            // 动作后收集：收集满 20 帧作为操作后对照组
            postActionBuffer.add(frameVar);
            postActionCount++;

            if (postActionCount >= 20) {
                // 收集完毕！向 Python 发射双黄蛋！
                agentClient.sendReflexionDiff(preActionBuffer, postActionBuffer, shadowFreqs);

                // 恢复正常巡航
                postActionBuffer.clear();
                shadowState.store(ShadowState::Normal);
                DBG("Shadow Recorder: Post-Action collected. Diff sent to Python!");
            }
        }
    }
}

std::vector<float> PluginProcessor::getCurrentBandFreqs() {
    std::vector<float> freqs;
    for (int i = 0; i < 24; ++i) {
        auto* param = parameters_.getRawParameterValue("freq" + juce::String(i));
        freqs.push_back(param != nullptr ? param->load() : 1000.0f);
    }
    return freqs;
}

void PluginProcessor::triggerShadowReflexion() {
    // 只有当环形缓冲区攒够了 1 秒的有效数据，我们才进行 A/B 对比
    if (shadowRingBuffer.size() == 20) {
        preActionBuffer = shadowRingBuffer; // 瞬间锁存！这就是原汁原味的操作前数据
        shadowRingBuffer.clear(); // 清空环形缓冲，防止旧数据污染
        postActionBuffer.clear();

        shadowFreqs.clear();
        for (float f : kStandardSpectrumGrid) {
            shadowFreqs.add(f);
        }

        postActionCount = 0;
        shadowState.store(ShadowState::CollectingPostAction);
        DBG("Shadow Recorder: Pre-Action locked. Waiting for Post-Action...");
    } else {
        DBG("Shadow Recorder: Not enough valid frames to lock Pre-Action. Skipping A/B diff.");
    }
}
