<div align="right"><a href="README_CN.md">简体中文</a></div>

# LingoEQ Plugin - C++/JUCE Component

> The real-time audio processing component of [LingoEQ](https://github.com/yuzh16/LingoEQ-agent) - a natural language driven parametric EQ system.
>
> This repository is a modified fork of [ZL Equalizer](https://github.com/ZL-Audio/ZLEqualizer) by [zsliu98](https://github.com/zsliu98). All modifications are by [yuzh16](https://github.com/yuzh16) and licensed under AGPLv3.

[![License](https://img.shields.io/badge/License-AGPLv3-blue.svg)](https://opensource.org/license/agpl-v3)

---

## What Is This?

LingoEQ is a two-part system that lets you control a parametric EQ with plain language:

```
User: "The vocals sound nasal, clean it up"
  -> Python backend (LLM + RAG + guardrails)
    -> This plugin (real-time DSP + spectrum capture)
```

![LingoEQ Plugin](docs/demo.png)

Once loaded, the plugin window shows the 24-band EQ on the left and a chat panel on the right. Type a description of the sound you want — the AI backend interprets it and adjusts the EQ bands automatically. Bands you adjust manually are locked and protected from AI modifications.

**This repository** is the C++/JUCE plugin half. It handles:
- 24-band parametric EQ with real-time audio processing
- Spectrum analysis and snapshot capture (used for Reflexion closed-loop validation)
- A built-in chat panel UI for sending natural language commands
- TCP JSON-RPC communication with the Python AI backend

The Python backend (LLM reasoning, RAG, guardrails) lives in [LingoEQ-agent](https://github.com/yuzh16/LingoEQ-agent).

---

## System Architecture

```
+-------------------------------------------------+
|  LingoEQ Plugin (this repo)                     |
|                                                 |
|  +-------------+    +----------------------+   |
|  |  ChatPanel  |    |   PluginProcessor    |   |
|  |  (JUCE UI)  |    |   (audio thread)     |   |
|  +------+------+    +----------+-----------+   |
|         |                      |               |
|  +------+----------------------+-----------+   |
|  |           AgentClient                   |   |
|  |  (background thread, TCP JSON-RPC)      |   |
|  +------------------+-----------------------+   |
|                     | port 8888             |
+---------------------|-------------------------+
                       |
+---------------------v---------------------------+
|  LingoEQ Python Backend (LingoEQ-agent repo)    |
|  LLM + RAG + Shadow State + Guardrails          |
+-------------------------------------------------+
```

**Communication protocol**: newline-delimited JSON over TCP socket (port 8888).

| Method | Direction | Description |
|--------|-----------|-------------|
| `chat/send` | Plugin -> Python | Send user message, receive thought + reply + tool calls |
| `eq/sync` | Plugin -> Python | Notify Python when user manually adjusts a band (triggers band lock) |
| `audio/snapshot_upload` | Plugin -> Python | Upload spectrum frames to a slot |
| `audio/snapshot_delete` | Plugin -> Python | Delete a snapshot slot |
| `audio/reflexion_diff_upload` | Plugin -> Python | Upload pre/post comparison frames for closed-loop validation |

---

## Modifications from ZL Equalizer

The following files were added or modified on top of the original ZL Equalizer codebase:

| File | Description |
|------|-------------|
| `source/AgentClient.cpp/.hpp` | Background TCP client thread. Connects to the Python backend, sends JSON-RPC messages, and dispatches responses (text, tool calls, status) via callbacks to the UI and processor. |
| `source/SpectralAnalyzer.hpp` | FFT-based spectrum analyzer (2048-point Hann window, 1/3-octave binning). Captures pre/post snapshots for the Reflexion closed-loop validation loop. |
| `source/panel/chat_panel/chat_panel.cpp/.hpp` | In-plugin chat UI. Displays the message thread (user messages, AI replies, thought chains), status overlay, and Auto-Check toggle. Wired to `AgentClient` callbacks. |
| `source/PluginProcessor.cpp/.hpp` | Integrated `AgentClient` and `SpectralAnalyzer`; added `eq/sync` parameter mirroring, snapshot trigger logic, and 5-slot sensory capture system. |
| `source/PluginEditor.cpp/.hpp` | Added `ChatPanel` (AI chat UI) and `SensoryPanel` (5-slot spectrum snapshot manager) to the plugin window layout. |

All other DSP, filter, GUI, and state code is from the original ZL Equalizer project.

---

## Build from Source

### Prerequisites

- **Compiler**: Clang (`AppleClang 16+` or `LLVM/Clang 17+`) on macOS/Linux; MSVC (Visual Studio 2022 v17.4+) or Clang on Windows
- **cmake** 3.24.1+
- **ninja**

On Linux, install audio/GUI dependencies:

```console
sudo apt-get update && sudo apt install libasound2-dev libx11-dev libxinerama-dev libxext-dev libfreetype-dev libfontconfig1-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev
```

All other dependencies (JUCE, nlopt, KFR) are git submodules — no separate install needed.

### Clone and Build

```console
git clone https://github.com/yuzh16/LingoEQ-plugin
cd LingoEQ-plugin
git submodule update --init --recursive
```

**macOS / Linux:**
```console
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKFR_ENABLE_MULTIARCH=ON \
  -DKFR_ARCHS="sse2;avx;avx2" \
  -DZL_JUCE_FORMATS="VST3;LV2;Standalone" \
  -DZL_EQ_BAND_NUM=24 .
cmake --build Builds --config Release
```

**Windows (PowerShell):**
```console
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release -DKFR_ENABLE_MULTIARCH=ON -DKFR_ARCHS="sse2;avx;avx2" -DZL_JUCE_FORMATS="VST3;Standalone" -DZL_EQ_BAND_NUM=24 .
cmake --build Builds --config Release
```

> Note: LV2 is not supported on Windows. Use `VST3;Standalone` instead.

**Key build variables:**

| Variable | Default | Description |
|----------|---------|-------------|
| `ZL_JUCE_FORMATS` | `VST3;LV2;Standalone` (macOS: `VST3;AU;Standalone`) | Plugin formats to build |
| `ZL_EQ_BAND_NUM` | `24` | Number of EQ bands - must match the Python backend config |
| `KFR_ARCHS` | `"sse2;avx;avx2"` | SIMD targets. Use `"neon64"` on ARM. |

After building, find the plugin binaries under `Builds/ZLEqualizer_artefacts/Release/` and copy them to your system plugin folder manually.

---

## Running with the Python Backend

> **Important**: The plugin connects to the Python backend once at startup and does not reconnect automatically. You must start the Python backend **before** loading the plugin. If the backend is not running when the plugin loads, the chat panel will be non-functional until you reload the plugin.

1. Start the Python backend first (see [LingoEQ-agent](https://github.com/yuzh16/LingoEQ-agent)):
   ```console
   python main.py
   ```
2. Load the plugin in your DAW or the JUCE Standalone build.
3. The plugin connects to `127.0.0.1:8888` on startup.
4. Type in the chat panel to control the EQ.

---

## License

This project is licensed under **AGPLv3**, as found in [LICENSE.md](LICENSE.md).

Copyright (C) 2026 - [yuzh16](https://github.com/yuzh16) (LingoEQ modifications)

Copyright (C) 2023 - [zsliu98](https://github.com/zsliu98) (ZL Equalizer original)

The [ZL Audio logo](assets/zlaudio.svg) and [ZL Equalizer logo](assets/logo.svg) are not covered by this license.

### Dependencies

| Library | License | Author |
|---------|---------|--------|
| [JUCE](https://github.com/juce-framework/JUCE) | AGPLv3 / Commercial | JUCE Framework |
| [nlopt](https://github.com/stevengj/nlopt) | LGPLv3 | Steven G. Johnson |
| [kfr](https://github.com/kfrlib/kfr) | GPLv2 | KFR |
| [Material Symbols](https://github.com/google/material-design-icons) | Apache 2.0 | Google |
| [Inter](https://github.com/rsms/inter) | OFL | The Inter Project Authors |
| [pamplejuce](https://github.com/sudara/pamplejuce) | MIT | sudara |

---

## References

Vicanek, Martin. *Matched One-Pole Digital Shelving Filters*. (2019).

Vicanek, Martin. *Matched Second Order Digital Filters*. (2016).

Redmon, Nigel. *Cascading filters*. (2016).

Wishnick, Aaron. *Time-Varying Filters for Musical Applications*. DAFx. (2014).

Moler, Cleve. [*Makima Piecewise Cubic Interpolation*](https://blogs.mathworks.com/cleve/2019/04/29/makima-piecewise-cubic-interpolation/). MathWorks Blogs. (2019).