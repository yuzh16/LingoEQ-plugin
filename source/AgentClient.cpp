// Copyright (C) 2026 - yuzh16
// This file is part of LingoEQ (based on ZLEqualizer by zsliu98)
//
// LingoEQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// LingoEQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with LingoEQ. If not, see <https://www.gnu.org/licenses/>.

#include "AgentClient.hpp"

// 寮曞叆鍛藉悕绌洪棿
using namespace juce;

AgentClient::AgentClient() : Thread("LingoEQ_NetworkThread") {}

AgentClient::~AgentClient() { stop(); }

bool AgentClient::connectToServer(const String& hostName, int portNumber) {
    if (socket.isConnected())
        socket.close();

    // 灏濊瘯杩炴帴
    bool success = socket.connect(hostName, portNumber, 3000);

    if (success) {
        connectionStatus = true;
        startThread(); // 鍚姩绾跨▼
        DBG("AgentClient: Connected to " + hostName);
    } else {
        connectionStatus = false;
        DBG("AgentClient: Connection failed.");
    }

    return success;
}

void AgentClient::stop() {
    signalThreadShouldExit();
    socket.close();
    stopThread(4000);
    connectionStatus = false;
}

bool AgentClient::isConnected() const { return connectionStatus; }

void AgentClient::sendUserMessage(const String& text,bool autoCheck) {
    if (!connectionStatus)
        return;

    // 1. 鏋勯€?JSON
    auto* params = new DynamicObject();
    params->setProperty("content", text);

    // 馃憠 銆愭柊澧炪€戞妸鑷寮€鍏崇姸鎬佸彂缁?Python
    params->setProperty("auto_check", autoCheck);

    auto* root = new DynamicObject();
    root->setProperty("method", "chat/send");
    root->setProperty("params", params);

    // 2. 搴忓垪鍖?
    String jsonString = JSON::toString(var(root), true);
    jsonString += "\n"; // 鍔犱笂鎹㈣绗︿綔涓虹粨鏉熸爣璁?

    // 3. 鍙戦€?
    socket.write(jsonString.toRawUTF8(), (int)jsonString.getNumBytesAsUTF8());
}

// ------------------------------------------------------------
// [鏍稿績閫昏緫] 鎵嬪姩璇诲彇 Socket 鏁版嵁娴?
// ------------------------------------------------------------
void AgentClient::run() {
    // 瀹氫箟涓€涓湰鍦扮紦鍐插尯锛岀敤浜庣紦瀛樻帴鏀跺埌鐨勪笉瀹屾暣鏁版嵁
    String lineBuffer;

    // 涓存椂璇诲彇鐢ㄧ殑鍐呭瓨鍧?(4KB)
    const int kBufferSize = 4096;
    HeapBlock<char> readBuffer(kBufferSize);

    while (!threadShouldExit()) {
        // 1. 妫€鏌ヨ繛鎺ョ姸鎬?
        if (!socket.isConnected()) {
            connectionStatus = false;
            wait(500); // 閬垮厤绌鸿浆
            continue;
        }

        // 2. 绛夊緟鏁版嵁 (瓒呮椂 100ms)
        if (socket.waitUntilReady(true, 100) > 0) {
            // 3. 璇诲彇鍘熷瀛楄妭 [宸蹭慨澶嶏細娣诲姞 false 鍙傛暟]
            // false 琛ㄧず闈為樆濉炴ā寮忥細鏈夊灏戣澶氬皯锛屼笉瑕佺瓑鍒板～婊＄紦鍐插尯鎵嶈繑鍥?
            int bytesRead = socket.read(readBuffer.getData(), kBufferSize - 1, false);

            if (bytesRead > 0) {
                // 纭繚 safe
                readBuffer[bytesRead] = 0;

                // 灏嗘柊鏀跺埌鐨勬暟鎹嫾鎺ュ埌缂撳瓨鏈熬
                lineBuffer += String::createStringFromData(readBuffer.getData(), bytesRead);

                // 4. 寰幆鎻愬彇瀹屾暣鐨勮 (浠?\n 缁撳熬)
                while (true) {
                    int newlinePos = lineBuffer.indexOfChar('\n');

                    if (newlinePos >= 0) {
                        // 鎻愬彇绗竴琛?
                        String currentLine = lineBuffer.substring(0, newlinePos);

                        // 浠庣紦瀛樹腑绉婚櫎宸插鐞嗙殑杩欒
                        lineBuffer = lineBuffer.substring(newlinePos + 1);

                        // 澶勭悊娑堟伅
                        if (currentLine.isNotEmpty()) {
                            handleServerResponse(currentLine);
                        }
                    } else {
                        // 缂撳瓨閲屾病鏈夋崲琛岀浜嗭紝璇存槑鍓╀笅鐨勬暟鎹笉瀹屾暣锛岀瓑涓嬩竴娆¤鍙?
                        break;
                    }
                }
            } else if (bytesRead < 0) {
                // 璇诲彇杩斿洖璐熸暟锛岄€氬父鎰忓懗鐫€杩炴帴琚鏂规柇寮€浜?
                connectionStatus = false;
                socket.close();
            }
        }
    }
}

void AgentClient::handleServerResponse(const String& jsonLine) {
    // 瑙ｆ瀽 JSON
    var result = JSON::parse(jsonLine);

    if (!result.isObject())
        return;

    // 鎻愬彇鍗忚鍐呭
    if (result.hasProperty("result")) {
        var resultObj = result["result"];

        if (resultObj.hasProperty("content") && resultObj["content"].isArray()) {
            var contentArray = resultObj["content"];

            for (int i = 0; i < contentArray.size(); ++i) {
                var item = contentArray[i];
                String type = item["type"];

                if (type == "text") {
                    String role = item["role"];
                    String text = item["text"];

                    if (onMessageReceived)
                        onMessageReceived(role, text);
                } else if (type == "tool_use") {
                    if (onToolCallReceived)
                        onToolCallReceived(item);
                }
                // 銆愭柊澧炪€戣В鏋愮姸鎬佹寚浠?
                else if (type == "status") {
                    juce::String statusCode = item["status_code"];
                    juce::String textMsg = item["message"];
                    if (onStatusReceived) {
                        onStatusReceived(statusCode, textMsg);
                    }
                }
            }
        }
    }
}

// [LingoEQ Integration] 鍙嶅悜鍚屾鍙戦€佸嚱鏁?
void AgentClient::sendSyncParameter(int bandIdx, const String& paramID, const var& value) {
    if (!connectionStatus)
        return;

    // 1. 鏋勯€?params 瀵硅薄 { "band": 0, "param": "freq", "value": 100.0 }
    auto* params = new DynamicObject();
    params->setProperty("band", bandIdx);
    params->setProperty("param", paramID);
    params->setProperty("value", value);

    // 2. 鏋勯€?JSON-RPC 鍖?
    auto* root = new DynamicObject();
    root->setProperty("method", "eq/sync"); // Python 绔搴旂殑 method
    root->setProperty("params", params);

    // 3. 搴忓垪鍖栧苟鍙戦€?
    String jsonString = JSON::toString(var(root), true);
    jsonString += "\n";

    // 鍐欏叆 Socket
    socket.write(jsonString.toRawUTF8(), (int)jsonString.getNumBytesAsUTF8());
}

void AgentClient::sendSnapshot(const juce::Array<juce::var>& frames, const juce::Array<juce::var>& freqList,
                               int targetSlot) {
    if (!isConnected())
        return;

    auto* params = new DynamicObject();
    params->setProperty("frames", frames);
    params->setProperty("frequencies", freqList);
    params->setProperty("slot_index", targetSlot); // 銆愭柊澧炪€戝憡璇?Python 瀛樺湪鍑犲彿妲戒綅

    auto* root = new DynamicObject();
    root->setProperty("method", "audio/snapshot_upload");
    root->setProperty("params", params);

    String jsonString = JSON::toString(var(root), true) + "\n";
    socket.write(jsonString.toRawUTF8(), (int)jsonString.getNumBytesAsUTF8());
}

// 銆愭柊澧炲嚱鏁般€戦€氱煡 Python 鍒犻櫎鎸囧畾妲戒綅
void AgentClient::sendDeleteSnapshot(int targetSlot) {
    if (!isConnected())
        return;

    auto* params = new DynamicObject();
    params->setProperty("slot_index", targetSlot);

    auto* root = new DynamicObject();
    root->setProperty("method", "audio/snapshot_delete"); // Python 绔搴旂殑鍒犻櫎 method
    root->setProperty("params", params);

    String jsonString = JSON::toString(var(root), true) + "\n";
    socket.write(jsonString.toRawUTF8(), (int)jsonString.getNumBytesAsUTF8());
}

void AgentClient::sendReflexionDiff(const juce::Array<juce::var>& preFrames, const juce::Array<juce::var>& postFrames,
                                    const juce::Array<juce::var>& freqList) {
    if (!isConnected())
        return;

    auto* params = new juce::DynamicObject();
    params->setProperty("pre_frames", preFrames);
    params->setProperty("post_frames", postFrames);
    params->setProperty("frequencies", freqList);

    auto* root = new juce::DynamicObject();
    root->setProperty("method", "audio/reflexion_diff_upload");
    root->setProperty("params", params);

    juce::String jsonString = juce::JSON::toString(juce::var(root), true) + "\n";
    socket.write(jsonString.toRawUTF8(), (int)jsonString.getNumBytesAsUTF8());
}
