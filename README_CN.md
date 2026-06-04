# LingoEQ Plugin - C++/JUCE 插件端

> 本仓库是 [LingoEQ](https://github.com/yuzh16/LingoEQ-agent) 的实时音频处理组件——一个自然语言驱动的参数均衡器系统。
>
> 本仓库基于 [zsliu98](https://github.com/zsliu98) 的 [ZL Equalizer](https://github.com/ZL-Audio/ZLEqualizer) 修改而来。改动由 [yuzh16](https://github.com/yuzh16) 完成，遵循 AGPLv3 协议。

[![License](https://img.shields.io/badge/License-AGPLv3-blue.svg)](https://opensource.org/license/agpl-v3)

<div align="right"><a href="README.md">English</a></div>

---

## 这是什么？

LingoEQ 是一个两部分组成的系统，让你用自然语言控制参数均衡器：

```
用户："人声有鼻音，清一下"
  -> Python 后端（LLM + RAG + 守护栏）
    -> 本插件（实时 DSP + 频谱采集）
```

![LingoEQ Plugin](docs/demo.png)

插件加载后，窗口左侧是 24 段 EQ 曲线，右侧是聊天面板。用自然语言描述你想要的音色，AI 后端会自动解读并调整对应的 EQ 频段。你手动调整过的频段会被锁定，AI 不会覆盖它们。

**本仓库**是 C++/JUCE 插件端，负责：
- 24 段参数均衡器实时音频处理
- 频谱分析与快照采集（用于 Reflexion 闭环验证）
- 内置聊天面板 UI，用于发送自然语言指令
- 通过 TCP JSON-RPC 与 Python AI 后端通信

Python 后端（LLM 推理、RAG、守护栏逻辑）位于 [LingoEQ-agent](https://github.com/yuzh16/LingoEQ-agent)。

---

## 系统架构

```
+-------------------------------------------------+
|  LingoEQ Plugin（本仓库）                        |
|                                                 |
|  +-------------+    +----------------------+   |
|  |  ChatPanel  |    |   PluginProcessor    |   |
|  |  （JUCE UI）|    |   （音频线程）        |   |
|  +------+------+    +----------+-----------+   |
|         |                      |               |
|  +------+----------------------+-----------+   |
|  |           AgentClient                   |   |
|  |  （后台线程，TCP JSON-RPC）              |   |
|  +------------------+----------------------+   |
|                     | 端口 8888               |
+---------------------|-------------------------+
                       |
+---------------------v---------------------------+
|  LingoEQ Python 后端（LingoEQ-agent 仓库）       |
|  LLM + RAG + 影子状态 + 守护栏                  |
+-------------------------------------------------+
```

**通信协议**：基于 TCP Socket（端口 8888）的换行符分隔 JSON 消息。

| 方法 | 方向 | 描述 |
|------|------|------|
| `chat/send` | 插件 -> Python | 发送用户消息，接收思维链 + 回复 + 工具调用 |
| `eq/sync` | 插件 -> Python | 用户手动调整频段时通知 Python（触发频段锁定）|
| `audio/snapshot_upload` | 插件 -> Python | 上传频谱帧到指定槽位 |
| `audio/snapshot_delete` | 插件 -> Python | 删除指定槽位快照 |
| `audio/reflexion_diff_upload` | 插件 -> Python | 上传调参前后对比帧，触发闭环验证 |

---

## 相对 ZL Equalizer 的改动

以下文件是在原版 ZL Equalizer 基础上新增或修改的：

| 文件 | 描述 |
|------|------|
| `source/AgentClient.cpp/.hpp` | 后台 TCP 客户端线程。连接 Python 后端，发送 JSON-RPC 消息，并通过回调将响应（文本、工具调用、状态码）分发给 UI 和处理器。|
| `source/SpectralAnalyzer.hpp` | 基于 FFT 的频谱分析器（2048 点 Hann 窗，1/3 倍频程分组）。采集调参前后的频谱快照，用于 Reflexion 闭环验证。|
| `source/panel/chat_panel/chat_panel.cpp/.hpp` | 插件内置聊天 UI。显示消息列表（用户消息、AI 回复、思维链）、状态浮层和自动验证开关，通过 AgentClient 回调驱动。|
| `source/PluginProcessor.cpp/.hpp` | 集成了 AgentClient 和 SpectralAnalyzer；增加了 eq/sync 参数镜像、快照触发逻辑和 5 槽位感知采集系统。|
| `source/PluginEditor.cpp/.hpp` | 将 ChatPanel（AI 聊天 UI）和 SensoryPanel（5 槽位频谱快照管理器）加入插件窗口布局。|

其余所有 DSP、滤波器、GUI 和状态管理代码均来自原版 ZL Equalizer 项目。

---

## 从源码构建

### 前置依赖

- **编译器**：macOS/Linux 使用 Clang（推荐 `AppleClang 16+` 或 `LLVM/Clang 17+`）；Windows 使用 MSVC（Visual Studio 2022 v17.4+）或 Clang
- **cmake** 3.24.1+
- **ninja**

Linux 下安装音频/GUI 依赖：

```console
sudo apt-get update && sudo apt install libasound2-dev libx11-dev libxinerama-dev libxext-dev libfreetype-dev libfontconfig1-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev
```

其余依赖（JUCE、nlopt、KFR）均为 git submodule，无需单独安装。

### 克隆与构建

```console
git clone https://github.com/yuzh16/LingoEQ-plugin
cd LingoEQ-plugin
git submodule update --init --recursive
```

**macOS / Linux：**
```console
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKFR_ENABLE_MULTIARCH=ON \
  -DKFR_ARCHS="sse2;avx;avx2" \
  -DZL_JUCE_FORMATS="VST3;LV2;Standalone" \
  -DZL_EQ_BAND_NUM=24 .
cmake --build Builds --config Release
```

**Windows（PowerShell）：**
```console
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release -DKFR_ENABLE_MULTIARCH=ON -DKFR_ARCHS="sse2;avx;avx2" -DZL_JUCE_FORMATS="VST3;Standalone" -DZL_EQ_BAND_NUM=24 .
cmake --build Builds --config Release
```

> 注：Windows 不支持 LV2 格式，请使用 `VST3;Standalone`。

**关键构建变量：**

| 变量 | 默认值 | 描述 |
|------|--------|------|
| `ZL_JUCE_FORMATS` | `VST3;LV2;Standalone`（macOS：`VST3;AU;Standalone`）| 构建的插件格式 |
| `ZL_EQ_BAND_NUM` | `24` | EQ 频段数量，必须与 Python 后端配置一致 |
| `KFR_ARCHS` | `"sse2;avx;avx2"` | SIMD 指令集目标。ARM 上使用 `"neon64"`。|

构建完成后在 `Builds/ZLEqualizer_artefacts/Release/` 下找到插件二进制文件，手动复制到系统插件目录。

---

## 与 Python 后端配合运行

> **注意**：插件在启动时连接一次 Python 后端，之后不会自动重连。必须在加载插件**之前**先启动 Python 后端。如果插件加载时后端未运行，聊天面板将无法使用，需要重新加载插件。

1. 先启动 Python 后端（参见 [LingoEQ-agent](https://github.com/yuzh16/LingoEQ-agent)）：
   ```console
   python main.py
   ```
2. 在 DAW 中加载插件，或使用 JUCE Standalone 构建版本。
3. 插件启动时连接 `127.0.0.1:8888`。
4. 在聊天面板中输入自然语言指令控制 EQ。

---

## 许可证

本项目遵循 **AGPLv3** 协议，详见 [LICENSE.md](LICENSE.md)。

Copyright (C) 2026 - [yuzh16](https://github.com/yuzh16)（LingoEQ 改动部分）

Copyright (C) 2023 - [zsliu98](https://github.com/zsliu98)（ZL Equalizer 原版）

[ZL Audio 图标](assets/zlaudio.svg) 和 [ZL Equalizer 图标](assets/logo.svg) 不在本协议覆盖范围内。

### 依赖库

| 库 | 协议 | 作者 |
|----|------|------|
| [JUCE](https://github.com/juce-framework/JUCE) | AGPLv3 / 商业 | JUCE Framework |
| [nlopt](https://github.com/stevengj/nlopt) | LGPLv3 | Steven G. Johnson |
| [kfr](https://github.com/kfrlib/kfr) | GPLv2 | KFR |
| [Material Symbols](https://github.com/google/material-design-icons) | Apache 2.0 | Google |
| [Inter](https://github.com/rsms/inter) | OFL | The Inter Project Authors |
| [pamplejuce](https://github.com/sudara/pamplejuce) | MIT | sudara |

---

## 参考文献

Vicanek, Martin. *Matched One-Pole Digital Shelving Filters*. (2019).

Vicanek, Martin. *Matched Second Order Digital Filters*. (2016).

Redmon, Nigel. *Cascading filters*. (2016).

Wishnick, Aaron. *Time-Varying Filters for Musical Applications*. DAFx. (2014).

Moler, Cleve. [*Makima Piecewise Cubic Interpolation*](https://blogs.mathworks.com/cleve/2019/04/29/makima-piecewise-cubic-interpolation/). MathWorks Blogs. (2019).
