锘?/ Copyright (C) 2025 - zsliu98
// Copyright (C) 2026 - yuzh16 (LingoEQ modifications)
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "../helper/helper.hpp"
#include "../../gui/gui.hpp"

namespace zlpanel {
    // Forward declaration
    class MessageContainer;

    // 馃憠 銆愮粓鏋佹潃鎵嬮攺銆戝畬鍏ㄨ劚绂?Label 鍖呰⒈鐨勭函鍑€鐘舵€佹皵娉＄粍浠?    class StatusBubble : public juce::Component {
    public:
        StatusBubble() { setOpaque(false); }

        // 鎴戜滑鑷繁鎻愪緵涓€涓粷瀵圭敓鏁堢殑璁剧疆鏂规硶
        void setStatus(const juce::String& text, juce::Colour colour, juce::Font font) {
            currentText = text;
            currentColour = colour;
            currentFont = font;
            repaint(); // 寮哄埗瑙﹀彂 JUCE 閲嶇粯寮曟搸锛?        }

        void paint(juce::Graphics& g) override {
            auto bounds = getLocalBounds().toFloat();

            // 1. 鐢诲畬缇庣殑鍗婇€忔槑榛戝簳鑳跺泭
            g.setColour(juce::Colours::black.withAlpha(0.7f));
            g.fillRoundedRectangle(bounds, bounds.getHeight() / 2.0f);

            // 2. 绾墜鍔ㄧ敤鑷繁鐨勯鑹插拰瀛椾綋鐢诲瓧锛岀粷涓嶄緷璧栫郴缁熺毊鑲わ紒
            if (currentText.isNotEmpty()) {
                g.setColour(currentColour);
                g.setFont(currentFont);
                // 宸﹀彸缂╄繘 8 鍍忕礌閬垮厤璐磋竟锛屼繚璇佺粷瀵硅兘鐢诲嚭鏉?                g.drawText(currentText, getLocalBounds().reduced(8, 0), juce::Justification::centred, true);
            }
        }

    private:
        juce::String currentText;
        juce::Colour currentColour{juce::Colours::white};
        juce::Font currentFont{14.0f};
    };
    
    class ChatPanel final : public juce::Component, private juce::Button::Listener, private juce::TextEditor::Listener {
    public:
        explicit ChatPanel(PluginProcessor& p, zlgui::UIBase& base);
        
        ~ChatPanel() override;
        
        void resized() override;
        
        void paint(juce::Graphics& g) override;
        
        void addMessage(const juce::String& message, bool isUser = false);
        
    private:
        PluginProcessor& p_ref_;
        zlgui::UIBase& base_;
        StatusBubble statusOverlay_;
        juce::ToggleButton autoCheckToggle_{"Auto-Check"};
        int currentCheckingId = 0; // 馃憠 銆愭柊澧炪€戠敤浜庤拷韪綋鍓嶆槸鍚﹀浜庤秴鏃剁姸鎬?        
        juce::Viewport messageViewport_;
        class MessageContainer : public juce::Component {
        public:
            MessageContainer(zlgui::UIBase& base);
            void addMessage(const juce::String& text, bool isUser);
            void updateLayout();
            void paint(juce::Graphics& g) override;
            void resized() override;
            void removeLastMessage();
            void removeLoadingMessage();

        private:
            struct ChatMessage {
                juce::String text;
                bool isUser;
                int height;
            };
            zlgui::UIBase& base_;
            std::vector<ChatMessage> messages_;
        } messageContainer_;
        
        juce::TextEditor inputEditor_;
        juce::TextButton sendButton_;
        
        void buttonClicked(juce::Button* button) override;
        
        void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
        
        void sendMessage();
        
        void setupSocketCommunication();
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPanel)
    };
} // zlpanel