// Copyright (C) 2026 - yuzh16
// This file is part of LingoEQ (based on ZLEqualizer by zsliu98)
//
// LingoEQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// LingoEQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with LingoEQ. If not, see <https://www.gnu.org/licenses/>.

#pragma once
// === [淇敼] 鏇挎崲 <JuceHeader.h> 涓烘ā鍧楀寲寮曠敤 ===
// AgentClient 涓昏鐢ㄥ埌 String, Thread, Socket, JSON锛岃繖浜涢兘鍦?juce_core 妯″潡涓?
#include <juce_core/juce_core.h>
#include <functional> // 寮曞叆 std::function
#include <atomic>

// 1. 缁ф壙 juce::Thread
// 鍘熺悊锛欽UCE 鐨勭嚎绋嬪皝瑁呯被銆傜户鎵垮畠璁╂垜浠嫢鏈変竴涓嫭绔嬬殑鎵ц娴併€?
class AgentClient : public juce::Thread {
public:
    // 鏋勯€犲嚱鏁?
    AgentClient();

    // 鏋愭瀯鍑芥暟锛氱‘淇濋攢姣佹椂鏂紑杩炴帴骞跺仠姝㈢嚎绋?
    ~AgentClient() override;

    // ============================================================
    // 瀵瑰鍏紑鐨勬帶鍒舵帴鍙?(Public API)
    // ============================================================

    /**
     * 杩炴帴鍒?Python 鏈嶅姟鍣?
     * @param hostName 鏈嶅姟鍣?IP (濡?"127.0.0.1")
     * @param portNumber 鏈嶅姟鍣ㄧ鍙?
     * @return 鏄惁杩炴帴鎴愬姛
     */
    bool connectToServer(const juce::String& hostName, int portNumber);

    /**
     * 鍙戦€佺敤鎴锋秷鎭?
     * 鍘熺悊锛氬皢鏂囨湰灏佽鎴?JSON 鍗忚骞跺啓鍏?Socket
     */
    void sendUserMessage(const juce::String& text, bool autoCheck = true);

    /**
     * 涓诲姩鍋滄
     * 鐢ㄤ簬鎻掍欢閿€姣佹椂瀹夊叏閫€鍑?
     */
    void stop();

    /**
     * 妫€鏌ヨ繛鎺ョ姸鎬?
     */
    bool isConnected() const;

    // ============================================================
    // 鍥炶皟绯荤粺 (The Callback System) - 瑙傚療鑰呮ā寮忕殑绠€鍖栫増
    // ============================================================
    // 鍘熺悊锛欰gentClient 涓嶅簲璇ョ煡閬?"ChatPanel" 鎴?"PluginProcessor" 鐨勫瓨鍦ㄣ€?
    // 瀹冨彧璐熻矗鏀舵秷鎭紝鏀跺埌鍚庨€氳繃杩欎簺鍑芥暟鎸囬拡鈥滃箍鎾€濆嚭鍘汇€?
    // 璋佸叧蹇冩秷鎭紝璋佸氨缁戝畾杩欎釜鍑芥暟銆?

    // 褰撴敹鍒版枃鏈洖澶嶆椂瑙﹀彂 (role: "assistant"|"thought", content: 鏂囨湰鍐呭)
    std::function<void(const juce::String& /*role*/, const juce::String& /*content*/)> onMessageReceived;

    // 褰撴敹鍒板伐鍏疯皟鐢ㄦ寚浠ゆ椂瑙﹀彂 (json object) -> 鐢ㄤ簬鎺у埗 EQ 鍙傛暟
    std::function<void(const juce::var& /*toolJson*/)> onToolCallReceived;
    std::function<void(const juce::String&, const juce::String&)> onStatusReceived;

    // 鍙戦€佸弽鍚戝悓姝ヤ俊鍙?(Band Index, Param Name, Value)
    void sendSyncParameter(int bandIdx, const juce::String& paramID, const juce::var& value);

    void sendSnapshot(const juce::Array<juce::var>& frames, const juce::Array<juce::var>& freqList,int targetSlot); // 娉ㄦ剰杩欓噷鍔犱簡 int targetSlot

    // 鍙戦€佸垹闄ゅ揩鐓ф寚浠?
    void sendDeleteSnapshot(int targetSlot);

    // [鏂板] 鍙戦€?A/B 闂幆瀵规瘮鏁版嵁鍖?(鎿嶄綔鍓?20 甯?vs 鎿嶄綔鍚?20 甯?
    // [淇敼] 鍙戦€?A/B 闂幆瀵规瘮鏁版嵁鍖?(鎿嶄綔鍓?20 甯?vs 鎿嶄綔鍚?20 甯?
    void sendReflexionDiff(const juce::Array<juce::var>& preFrames, const juce::Array<juce::var>& postFrames,
                           const juce::Array<juce::var>& freqList);

private:
    // ============================================================
    // 鍐呴儴瀹炵幇 (Internal Implementation)
    // ============================================================

    // 绾跨▼鐨勪富寰幆鍑芥暟 (蹇呴』瀹炵幇)
    // 瀹冩槸鍚庡彴绾跨▼鐨勫叆鍙ｇ偣锛屽氨鍍?main() 鍑芥暟涓€鏍?
    void run() override;

    // 澶勭悊骞惰В鏋愬崟琛?JSON 瀛楃涓?
    void handleServerResponse(const juce::String& jsonLine);

    // 鏍稿績鎴愬憳鍙橀噺锛歍CP 濂楁帴瀛?
    // StreamingSocket 鏄?JUCE 瀵瑰簳灞?socket 鐨勫皝瑁咃紝鎻愪緵 connect, read, write
    juce::StreamingSocket socket;

    // 绠€鍗曠殑鐘舵€佹爣蹇?
    std::atomic<bool> connectionStatus{false}; // 浣跨敤 atomic 淇濊瘉绾跨▼瀹夊叏
};
