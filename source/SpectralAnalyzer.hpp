// Copyright (C) 2026 - yuzh16
// This file is part of LingoEQ (based on ZLEqualizer by zsliu98)
//
// LingoEQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// LingoEQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with LingoEQ. If not, see <https://www.gnu.org/licenses/>.

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

class SpectralAnalyzer {
public:
    static constexpr int fftOrder = 11; // 2048 点
    static constexpr int fftSize = 1 << fftOrder;

    SpectralAnalyzer() : forwardFFT(fftOrder), window(fftSize, juce::dsp::WindowingFunction<float>::hann) {}

    // [音频线程调用]：将实时采样点推入 FIFO
    void pushSample(float sample) {
        if (fifoIndex == fftSize) {
            // 将写满的数据转移到待处理区
            std::copy(fifo.begin(), fifo.end(), fftDataToProcess.begin());
            dataReady = true;
            fifoIndex = 0;
        }
        fifo[fifoIndex++] = sample;
    }

    // [主线程调用]：获取当前 24 个频段的归一化能量 (0.0 ~ 1.0)
    std::vector<float> getBinnedSpectrum(const std::vector<float>& centers, double sampleRate) {
        if (dataReady) {
            // 1. 在时域上应用汉宁窗
            window.multiplyWithWindowingTable(fftDataToProcess.data(), fftSize);

            // 2. 准备 FFT 数据 (JUCE 要求：前 fftSize 个为真实采样点，后 fftSize 个全补零)
            std::fill(complexFftData.begin(), complexFftData.end(), 0.0f);
            std::copy(fftDataToProcess.begin(), fftDataToProcess.end(), complexFftData.begin());

            // 3. 执行 FFT 变换，结果(线性幅值)会直接覆盖在 complexFftData 的前 fftSize/2 + 1 个元素中
            forwardFFT.performFrequencyOnlyForwardTransform(complexFftData.data());
        }

        std::vector<float> bins;
        float freqResolution = static_cast<float>(sampleRate) / fftSize;

        // [科研级升级] 采用 1/3 倍频程 (1/3 Octave) 的带宽比例进行能量积分
        // 上下限系数：2^(1/6) 约为 1.12246， 2^(-1/6) 约为 0.89090
        const float lowerBoundFactor = 0.8908987f;
        const float upperBoundFactor = 1.1224620f;

        for (float f : centers) {
            // 动态计算当前频段的掩蔽/积分上下限频率
            float fLower = f * lowerBoundFactor;
            float fUpper = f * upperBoundFactor;

            // 找到对应的 FFT bin 索引范围
            int idxStart = static_cast<int>(fLower / freqResolution);
            int idxEnd = static_cast<int>(std::ceil(fUpper / freqResolution));

            // 边界保护，防止越界
            idxStart = juce::jlimit(0, fftSize / 2 - 1, idxStart);
            idxEnd = juce::jlimit(idxStart, fftSize / 2 - 1, idxEnd);

            // 如果频带非常窄（通常在极低频区），至少保证取一个 bin
            if (idxEnd == idxStart)
                idxEnd = idxStart + 1;

            // 计算该频带内的总能量（幅值的平方和）
            float energySum = 0.0f;
            int count = 0;
            for (int i = idxStart; i < idxEnd; ++i) {
                float mag = complexFftData[i];
                energySum += (mag * mag); // 心理声学法则：能量相加
                count++;
            }

            // 取均方根 (Root Mean Square)
            float rmsMag = (count > 0) ? std::sqrt(energySum / count) : 0.0f;

            // 转换为分贝，并将 -100dB ~ 0dB 严格映射到 0.0 ~ 1.0
            float db = juce::Decibels::gainToDecibels(rmsMag, -100.0f);
            float normalized = juce::jmap(db, -100.0f, 0.0f, 0.0f, 1.0f);
            bins.push_back(juce::jlimit(0.0f, 1.0f, normalized));
        }
        return bins;
    }

private:
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize> fifo; // 音频线程写入缓冲区
    std::array<float, fftSize> fftDataToProcess; // 准备加窗的数据
    std::array<float, fftSize * 2> complexFftData; // 给 FFT 用的复数交织数组
    int fifoIndex = 0;
    std::atomic<bool> dataReady{false};
};
