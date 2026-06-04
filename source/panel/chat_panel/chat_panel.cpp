锘?/ Copyright (C) 2025 - zsliu98
// Copyright (C) 2026 - yuzh16 (LingoEQ modifications)
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.

#include "chat_panel.hpp"

namespace zlpanel {
    // Implement MessageContainer nested class
    ChatPanel::MessageContainer::MessageContainer(zlgui::UIBase& base) : base_(base) {
        setOpaque(true);
    }
    
    void ChatPanel::MessageContainer::addMessage(const juce::String& text, bool isUser) {
        messages_.push_back({text, isUser, 0});
        updateLayout();
    }
    
    void ChatPanel::MessageContainer::updateLayout() {
        int y = 15; // 椤堕儴鐣欎竴鐐硅竟璺?        int maxWidth = getWidth() - 40; // 姘旀场鏈€澶у搴︼紝涓よ竟鐣欏嚭绌洪棿
        if (maxWidth <= 0)
            maxWidth = 200; // 闃叉鍒濆鍖栨椂瀹藉害涓?鐨勯櫎闆跺穿婧?
        juce::Font font(base_.getFontSize() * 1.0f);

        for (auto& msg : messages_) {
            juce::AttributedString attrStr;
            attrStr.append(msg.text, font, base_.getTextColour());

            juce::TextLayout layout;
            // 鍏抽敭锛氬熀浜庢枃鏈唴瀹瑰拰鏈€澶у搴︼紝鐢熸垚鐪熷疄鐨勬枃瀛楁帓鐗?            layout.createLayout(attrStr, maxWidth - 32); // 32 鏄皵娉″唴閮ㄧ殑宸﹀彸 Padding

            // 姘旀场鐪熷疄楂樺害 = 鏂囧瓧鎺掔増楂樺害 + 涓婁笅 Padding (24)
            msg.height = static_cast<int>(layout.getHeight()) + 24;
            y += msg.height + 12; // 12 鏄袱涓皵娉′箣闂寸殑鍨傜洿闂磋窛
        }

        setSize(getWidth(), y + 10);
        repaint();
    }

    void ChatPanel::MessageContainer::removeLoadingMessage() {
        // 濡傛灉娑堟伅鍒楄〃涓嶄负绌猴紝涓旀渶鍚庝竴鏉℃秷鎭槸鎴戜滑鍒氬彂鐨勫崰浣嶇锛屽氨鍒犳帀瀹?        if (!messages_.empty() && messages_.back().text.startsWith("[Thinking...]")) {
            messages_.pop_back(); // 鍒犳帀鏈€鍚庝竴鏉?            updateLayout(); // 閲嶆柊鎺掔増骞跺埛鏂癠I
        }
    }

    
    void ChatPanel::MessageContainer::paint(juce::Graphics& g) {
        // 缁樺埗鑱婂ぉ闈㈡澘搴曡壊锛堜繚鎸佷笌涓荤晫闈㈤殧绂伙級
        g.fillAll(base_.getBackgroundColour().withAlpha(0.95f));

        int y = 15;
        int maxWidth = getWidth() - 40;
        juce::Font font(base_.getFontSize() * 1.0f);

        for (const auto& msg : messages_) {
            juce::AttributedString attrStr;

            // --- 鏍峰紡瀹氬埗锛氬鏋滄槸 AI 鐨勬€濊€冭繃绋?鎴?浜屾绾犻敊锛屼娇鐢ㄧ伆鑹插€炬枩瀛椾綋 ---
            if (msg.text.startsWith("[Thinking]") || msg.text.startsWith("[Auto-Correction]")) {
                attrStr.append(msg.text, font.italicised(), base_.getTextColour().withAlpha(0.6f));
            } else {
                attrStr.append(msg.text, font, base_.getTextColour().withAlpha(0.9f));
            }

            juce::TextLayout layout;
            layout.createLayout(attrStr, maxWidth - 24);

            // 鑾峰彇杩欐鏂囧瓧瀹為檯鍗犵敤鐨勫搴︼紙濡傛灉瀛楁暟灏戯紝姘旀场灏变細鐭級
            int actualTextWidth = static_cast<int>(layout.getWidth());
            int bubbleWidth = actualTextWidth + 32; // 鍔犱笂宸﹀彸鍐呰竟璺?
            // --- 闃佃惀鍖哄垎锛氱敤鎴峰湪鍙筹紝AI鍦ㄥ乏 ---
            int x = msg.isUser ? (getWidth() - bubbleWidth - 10) : 10;

            auto bounds = juce::Rectangle<int>(x, y, bubbleWidth, msg.height).toFloat();

            // --- 缁樺埗姘旀场鑳屾櫙 ---
            if (msg.isUser) {
                // 鐢ㄦ埛姘旀场锛氫娇鐢ㄥ己璋冭壊锛堟瘮濡傜◢寰寒涓€鐐圭殑閫忔槑鑹诧級
                g.setColour(base_.getColourByIdx(zlgui::kTextColour).withAlpha(0.15f));
                g.fillRoundedRectangle(bounds, 12.0f); // 12px 鍦嗚锛屾洿鐜颁唬
            } else {
                // AI 姘旀场锛氫娇鐢ㄦ繁鑹茶儗鏅紝绉婚櫎涔嬪墠鍍电‖鐨勭嚎鏉¤竟妗?                g.setColour(base_.getBackgroundColour().darker(0.3f));
                g.fillRoundedRectangle(bounds, 12.0f);
            }

            // --- 缁樺埗鏂囧瓧 ---
            // 杩欓噷鐨勫潗鏍囧姞涓婁簡 12锛屽疄鐜颁簡瀹岀編鐨勬枃瀛楀唴杈硅窛 (Padding)
            layout.draw(g, juce::Rectangle<float>(x + 16, y + 12, actualTextWidth, layout.getHeight()));

            y += msg.height + 12;
        }
    }

    void ChatPanel::MessageContainer::resized() {
        updateLayout();
    }

    void ChatPanel::MessageContainer::removeLastMessage() {
        if (!messages_.empty()) {
            messages_.pop_back();
            updateLayout();
        }
    }
    
    ChatPanel::ChatPanel(PluginProcessor& p, zlgui::UIBase& base) :
        p_ref_(p), base_(base), messageViewport_(), messageContainer_(base), inputEditor_(), sendButton_("Send") {

        // --- 瑙嗗浘瀹瑰櫒鍒濆鍖?---
        addAndMakeVisible(messageViewport_);
        messageViewport_.setViewedComponent(&messageContainer_, false);
        messageViewport_.setScrollBarsShown(true, false, false, false);
        messageViewport_.setScrollBarThickness(8);

        addAndMakeVisible(inputEditor_);
        addAndMakeVisible(sendButton_);

        // ============================================================
        // [UI 淇敼] 杈撳叆妗嗕笌鎸夐挳鐨勭編鍖?        // ============================================================
        inputEditor_.setMultiLine(false);
        inputEditor_.setReturnKeyStartsNewLine(false);
        inputEditor_.setScrollbarsShown(false);

        // 1. 鎭㈠浣犳渶寮€濮嬭兘鐪嬭鐨勩€愯儗鏅壊銆戝拰銆愯竟妗嗐€戯紝璁╄緭鍏ユ鍏堢幇鍑哄師褰紒
        inputEditor_.setColour(juce::TextEditor::backgroundColourId, base_.getBackgroundColour().brighter(0.1f));
        inputEditor_.setColour(juce::TextEditor::outlineColourId,
                               base_.getColourByIdx(zlgui::kTextColour).withAlpha(0.3f));
        inputEditor_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::white.withAlpha(0.5f));

        // 2. 寮哄埗缁欏畾涓€涓垵濮嬪瓧浣撳拰澶у皬锛堥槻姝㈠垵濮嬪寲鏃跺彉鎴?0 鍍忕礌瀵艰嚧闅愬舰锛?        inputEditor_.setFont(juce::Font(16.0f));

        // 3. 寮哄埗瀛椾綋棰滆壊涓虹函鐧?        inputEditor_.setColour(juce::TextEditor::textColourId, juce::Colours::white);

        // 4. 璁剧疆鎻愮ず璇?(娴呯櫧鑹诧紝闃叉澶殫)
        inputEditor_.setTextToShowWhenEmpty("Describe the sound (e.g. 'Make it warmer')...",
                                            juce::Colours::white.withAlpha(0.6f));


        inputEditor_.addListener(this);


        // 纭繚杈撳叆妗嗗唴鐨勬枃瀛椾篃鏄瀭鐩村眳涓殑
        inputEditor_.setJustification(juce::Justification::centredLeft);
        inputEditor_.setFont(juce::Font(base_.getFontSize() * 1.9f));

        // 浼樺寲鍙戦€佹寜閽細闄嶄綆绐佸厐鎰燂紝鎻愬崌涓庤緭鍏ユ鐨勬暣浣撴€?        sendButton_.setColour(juce::TextButton::buttonColourId,
                              base_.getColourByIdx(zlgui::kTextColour).withAlpha(0.15f));
        sendButton_.setColour(juce::TextButton::textColourOnId, base_.getTextColour());
        sendButton_.setColour(juce::TextButton::textColourOffId, base_.getTextColour());
        sendButton_.addListener(this);

        // Add welcome message
        addMessage("Hello! This is LingoEQ! May I hlep you?", false);

        // ============================================================
        // [LingoEQ Integration] 缁戝畾娑堟伅鎺ユ敹鍥炶皟
        // ============================================================
        p_ref_.agentClient.onMessageReceived = [this](const juce::String& role, const juce::String& content) {
            juce::MessageManager::callAsync([this, role, content]() {
                this->messageContainer_.removeLoadingMessage();

                // 馃憠 銆愭柊澧炰繚鎶ゃ€戞敹鍒版柊鐨勮亰澶╂皵娉℃椂锛岃鏄庨獙鏀舵祦绋嬬粨鏉燂紙鏃犺鎴愬姛澶辫触锛夛紝寮鸿闅愯棌鎮诞姘旀场
                this->currentCheckingId++;
                this->statusOverlay_.setVisible(false);

                if (role == "thought") {
                    this->addMessage("[Thinking]\n" + content, false);
                }
                // 馃憠 銆愭柊澧炶В鏋愩€戞嫤鎴簩娆￠獙璇佺殑姹囨姤姘旀场
                else if (role == "correction") {
                    this->addMessage("[Auto-Correction]\n" + content, false);
                } else {
                    this->addMessage(content, false);
                }
            });
        };
        // ============================================================
        // 鎮诞鐘舵€佹皵娉＄殑鍒濆鍖?        // ============================================================
        addChildComponent(statusOverlay_); // 榛樿闅愯棌

        // 缁戝畾 Status 鍥炶皟
        p_ref_.agentClient.onStatusReceived = [this](const juce::String& code, const juce::String& msg) {
            juce::MessageManager::callAsync([this, code, msg]() {
                // 馃憠 姣忔鏀跺埌鏂扮姸鎬侊紝璁?ID 鑷锛岃繖鏍峰氨鑳解€滀綔搴熲€濅箣鍓嶇殑瀹氭椂鍣?                this->currentCheckingId++;
                int expectedId = this->currentCheckingId;

                juce::Colour targetColor = (code == "success") ? juce::Colours::lightgreen : juce::Colours::orange;
                juce::Font targetFont(this->base_.getFontSize() * 1.1f, juce::Font::bold);

                this->statusOverlay_.setStatus(msg, targetColor, targetFont);
                this->statusOverlay_.setVisible(true);

                if (code == "success") {
                    // 鍚堟牸锛? 绉掑悗闅愯棌
                    juce::Timer::callAfterDelay(2000, [this, expectedId]() {
                        if (this->currentCheckingId == expectedId) {
                            this->statusOverlay_.setVisible(false);
                        }
                    });
                } else if (code == "checking") {
                    // 馃憠 銆愰槻鍋囨鏍稿績閫昏緫銆?                    // 濡傛灉 12s 鍚庯紝ID 娌″彉锛堟剰鍛崇潃鏃㈡病鏈夊彉鎴?success锛屼篃娌℃湁瑙﹀彂浜屾璋冨弬锛夛紝
                    // 鎴戜滑灏辨柇瀹氾細瀹夸富鏍规湰娌℃挱鏀鹃煶棰戯紒
                    juce::Timer::callAfterDelay(12000, [this, expectedId]() {
                        if (this->currentCheckingId == expectedId) {
                            // 寮鸿鎶婃皵娉″彉鎴愮伆鑹诧紝鎻愮ず鐢ㄦ埛
                            juce::Font warnFont(this->base_.getFontSize() * 1.0f, juce::Font::bold);
                            juce::String safeText(juce::CharPointer_UTF8("鈿狅笍 鏈娴嬪埌闊抽鎾斁锛屽凡璺宠繃闂幆楠岃瘉"));
                            this->statusOverlay_.setStatus(safeText, juce::Colours::grey.brighter(), warnFont);

                            // 鍋滅暀 2 绉掑悗褰诲簳闅愯棌
                            juce::Timer::callAfterDelay(2000, [this, expectedId]() {
                                if (this->currentCheckingId == expectedId) {
                                    this->statusOverlay_.setVisible(false);
                                }
                            });
                        }
                    });
                }
            });
        };
        // ============================================================
        // [鏂板] 鑷妯″紡寮€鍏冲垵濮嬪寲
        // ============================================================
        addAndMakeVisible(autoCheckToggle_);
        autoCheckToggle_.setToggleState(true, juce::dontSendNotification); // 榛樿寮€鍚?        autoCheckToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::grey);
        autoCheckToggle_.setColour(juce::ToggleButton::tickColourId,
                                   base_.getColourByIdx(zlgui::kTextColour).withAlpha(0.7f));
    }
    ChatPanel::~ChatPanel() {
        // [LingoEQ Integration] 蹇呴』瑙ｇ粦鍥炶皟锛?        // 鍚﹀垯绐楀彛鍏抽棴鍚庯紝鏀跺埌鏂版秷鎭細寮曞彂 Crash
        p_ref_.agentClient.onMessageReceived = nullptr;
        p_ref_.agentClient.onStatusReceived = nullptr;
    }
    
    void ChatPanel::resized() {
        auto bounds = getLocalBounds();
        auto buttonHeight = 40; // 鎸夐挳绋嶅井鍔犻珮锛屾洿鐜颁唬
        auto margin = 16; // 鍥涘懆鐣欑櫧鍔犲ぇ

        // 椤堕儴鐣欏嚭涓€鐐瑰懠鍚哥┖闂?        bounds.removeFromTop(10);

        // 鑱婂ぉ婊氬姩鍖?(Viewport) 鍗犳嵁涓昏绌洪棿
        messageViewport_.setBounds(bounds.removeFromTop(bounds.getHeight() - buttonHeight - margin * 2));
        messageContainer_.setSize(messageViewport_.getWidth(), messageContainer_.getHeight());

        bounds.removeFromTop(margin);

        // 搴曢儴杈撳叆鍖哄竷灞€锛氳杈撳叆妗嗗拰鎸夐挳鍍忔偓娴殑鑳跺泭涓€鏍峰乏鍙崇暀鍑?margin
        auto inputBounds = bounds.reduced(margin, 0).withHeight(buttonHeight);

        // 鎸夐挳鏀惧湪鍙宠竟锛屽搴?70
        sendButton_.setBounds(inputBounds.removeFromRight(70));
        inputBounds.removeFromRight(8); // 杈撳叆妗嗗拰鎸夐挳涔嬮棿鐣?8 鍍忕礌鐨勭紳闅?
        // 鍓╀笅鐨勫叏缁欒緭鍏ユ
        inputEditor_.setBounds(inputBounds);

        inputEditor_.setFont(juce::Font(base_.getFontSize() * 1.9f));

        autoCheckToggle_.setBounds(inputEditor_.getX(), inputEditor_.getY() - 24, 100, 20);
       // 姘旀场鍧愭爣璁＄畻锛氭洿骞虫粦銆侀潬涓娿€侀潬鍙?        auto overlayHeight = 28;

        // 鎴戜滑缁欐皵娉′竴涓悎鐞嗙殑鍥哄畾瀹藉害锛堟瘮濡?180锛夛紝闃叉瀹冨お闀跨洊浣?Thinking 姘旀场
        int overlayWidth = 180;
        int totalInputWidth = sendButton_.getRight() - inputEditor_.getX();
        int targetX = inputEditor_.getX() + (totalInputWidth - overlayWidth) / 2;

        // 璁＄畻 Y 鍧愭爣锛氳窛绂昏緭鍏ユ椤堕儴 12 鍍忕礌 (姣斿垰鎵嶇殑 8 鍍忕礌鏇撮潬涓婁竴涓€鐐?
        int targetY = inputEditor_.getY() - overlayHeight - 12;

        // 瀹夊叏妫€鏌ワ細闃叉鍒濆 Y 涓?0 鏃堕鍑哄睆骞?        if (targetY < 0)
            targetY = 80;

        // 璁剧疆鏂扮殑 Bounds
        statusOverlay_.setBounds(targetX, targetY, overlayWidth, overlayHeight);

        // 缁濆闃插尽锛氭诞鍦ㄦ渶涓婇潰
        statusOverlay_.toFront(false);
    }
    
    void ChatPanel::paint(juce::Graphics& g) {
        // 1. 缁樺埗鏁翠釜鍙充晶鑱婂ぉ闈㈡澘鐨勬繁鑹茶儗鏅簳鑹?        g.fillAll(base_.getBackgroundColour().darker(0.15f));

        // 2. 鍦ㄦ渶宸︿晶鐢讳竴鏉″瀭鐩寸殑榛戣壊鍗婇€忔槑闃村奖绾匡紝鐢ㄦ潵鍜屽乏渚х殑 EQ 棰戣氨闅斿紑
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));

        // 3. 缁樺埗杈撳叆妗嗕笂鏂圭殑閭ｆ潯姘村钩鍒嗗壊绾?        g.setColour(base_.getColourByIdx(zlgui::kTextColour).withAlpha(0.15f));
        g.drawLine(0.0f, static_cast<float>(messageViewport_.getBottom()), static_cast<float>(getWidth()),
                   static_cast<float>(messageViewport_.getBottom()), 1.0f);
    }
    
    void ChatPanel::addMessage(const juce::String& message, bool isUser) {
        messageContainer_.addMessage(message, isUser);
        
        // Scroll to bottom if viewport is initialized
        if (messageViewport_.getWidth() > 0 && messageViewport_.getHeight() > 0) {
            messageViewport_.setViewPosition(0, messageContainer_.getHeight() - messageViewport_.getHeight());
        }
    }
    
    void ChatPanel::buttonClicked(juce::Button* button) {
        if (button == &sendButton_) {
            sendMessage();
        }
    }
    
    void ChatPanel::textEditorReturnKeyPressed(juce::TextEditor& editor) {
        if (&editor == &inputEditor_) {
            sendMessage();
        }
    }
    
    void ChatPanel::sendMessage() {
        auto message = inputEditor_.getText().trim();
        if (!message.isEmpty()) {
            // 1. 鍦?UI 涓婃樉绀虹敤鎴峰彂鐨勮瘽
            addMessage(message, true);

            // 2. 娓呯┖杈撳叆妗?            inputEditor_.clear();

            // 3. 绔嬪嵆鏄剧ず涓€涓姸鎬佹寚绀哄櫒姘旀场锛堥潪鐢ㄦ埛闃佃惀锛?            addMessage("[Thinking...] Wait a minute...", false);
            bool isAutoCheck = autoCheckToggle_.getToggleState();
            // 馃憠 銆愭柊澧炪€戞妸鐘舵€佸悓姝ョ粰搴曞眰鐨?PluginProcessor锛?            p_ref_.autoCheckEnabled.store(isAutoCheck);
            p_ref_.agentClient.sendUserMessage(message,isAutoCheck);

        }
    }

    
    void ChatPanel::setupSocketCommunication() {
        // Here you would set up the socket connection to the backend
        // This is a placeholder for the actual implementation
        // You might want to use a separate thread for socket communication
    }
} // zlpanel