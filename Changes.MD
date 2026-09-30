# Linux Port Changes

Notes for the Windows-to-Linux work on ProjectCane (`Sly1-Release` + File Extractor). The Visual Studio Windows build is meant to keep working. Linux is built with CMake (`build-linux/`).

Temporary 8BitDo mapping work (`gamecontrollerdb.txt`, raw-axis fallback, `StartupGamepad` / `FillGamepadState`) was **added then fully removed** after analog drift. It is not in the tree now.

---

## Added files

### `CMakeLists.txt`
- **Added:** Linux CMake project for `Sly1-Release` and `FileExtractor`.
- **Why:** There was no non-MSVC build. This wires GLFW, OpenGL, FFmpeg, C++20, force-includes, `-iquote Sly1-Release` (so local `math.h` does not hide `<math.h>`), `ALO_IMPLEMENTATION` on `alo.cpp`, and miniaudio + `libm` on Linux.

### `Sly1-Release/host.h`
- **Added:** `ExecutableDirectory()`, `ReplaceFileAtomic()`, `HostDebugBreak()` (Linux only).
- **Why:** Replace `GetModuleFileNameW` / Win32 atomic replace / `__debugbreak` with portable helpers used by saves, shaders, worlds, and the extractor.

### `Sly1-Release/host.cpp`
- **Added:** implementations of the functions in `host.h` (`/proc/self/exe` on Linux, `GetModuleFileNameW` on Windows).
- **Why:** Same as `host.h`. Also listed in `Sly1-Release.vcxproj` so Windows still compiles it.

### `Sly1-Release/host_compat.h`
- **Added:** Linux stand-ins for `DWORD`, `_stricmp`, `__debugbreak()`, `YieldProcessor()`, `MAX_PATH`, etc.
- **Why:** Forced-include so MSVC-oriented sources compile with GCC without rewriting every file.

### `Sly1-Release/xaudio2_compat.h`
- **Added:** Minimal XAudio2 types (`IXAudio2`, source/master/submix voices, `WAVEFORMATEX`, `XAUDIO2_BUFFER`, reverb param structs) plus `XAudio2Create`, `XAudio2CreateReverb`, `ReverbConvertI3DL2ToNative`.
- **Why:** `sound.cpp` talks XAudio2. Linux has no Windows SDK, so the same call sites need a compatible API.

### `Sly1-Release/xaudio2_compat.cpp`
- **Added (then replaced dummy with real mixer):**
  - `XAudio2Create`, `XAudio2CreateReverb`, `ReverbConvertI3DL2ToNative`
  - `XAudio2Impl::CreateMasteringVoice` / `CreateSubmixVoice` / `CreateSourceVoice` / `Mix` / `MixVoiceFrame`
  - `SourceVoice::{SubmitSourceBuffer, FlushSourceBuffers, Start, Stop, GetState, SetVolume, SetFrequencyRatio, SetOutputMatrix, DestroyVoice}`
  - `MasteringVoice` / `SubmixVoice` volume, details, cheap delay reverb
- **Why:** First version was silent (compile only). Later filled in with miniaudio so music, SFX, dialog, and cutscene WAVs actually play. Windows still uses real XAudio2.

### `Sly1-Release/miniaudio_impl.c`
- **Added:** `#define MINIAUDIO_IMPLEMENTATION` + include of bundled `Libraries/include/miniaudio/miniaudio.h`.
- **Why:** One C translation unit for miniaudio so it is not compiled under the C++ force-includes.

### `Sly1-Release.vcxproj`
- **Edited:** added `host.cpp` to `ClCompile`.
- **Why:** Windows VS project must compile the new host helpers too.

---

## Build / GCC compatibility (bulk header edits)

### Many headers under `Sly1-Release/` (including `jack.h`, `Input.h`, `sound.h`, `difficulty.h`, `emitter.h`, …)
- **Edited:** `enum Name;` → `enum Name : int;` and `enum Name {` → `enum Name : int {`.
- **Why:** GCC rejects unscoped enum forward declarations and had signedness issues with `Nil = -1`. Forced `: int` matches MSVC’s usual underlying type.

### `Sly1-Release/mgv.cpp`
- **Edited:** include `"Input.h"` (correct case).
- **Why:** Linux filesystems are case-sensitive; `input.h` would not find `Input.h`.

### `Sly1-Release/mb.cpp`, `rythm.cpp`, `spaprops.cpp`
- **Edited:** `std::tanf` / `std::sinf` / `std::cosf` → `tan` / `sin` / `cos` (as needed).
- **Why:** libstdc++ does not put those C99 names in `std` the way MSVC does.

### `Sly1-Release/so.cpp`
- **Edited:** `#include <Windows.h>` wrapped in `#ifdef _WIN32`.
- **Why:** That header does not exist on Linux.

### `Sly1-Release/alo.cpp`
- **Edited (build only):** CMake defines `ALO_IMPLEMENTATION` on this file.
- **Why:** Same as the MSVC forced-include / single-TU pattern for ALO.

---

## Window, shaders, loop

### `Sly1-Release/main.h`
- **Edited:** includes `host_compat.h` instead of relying on Windows-only types.
- **Why:** `YieldProcessor` / DWORD-style types used by the frame pacer.

### `Sly1-Release/main.cpp`
- **Edited:** `FramePacer` constructor / destructor / `Wait()` — waitable timer only on `_WIN32`; Linux uses `sleep_until` + spin.
- **Edited:** `wWinMain` only on Windows release; Linux uses `main`.
- **Edited:** `InstallLiveResizeWindowProc` / Win32 thread priority stay `#ifdef _WIN32`.
- **Edited:** game loop calls `glfwPollEvents()` **before** `RunGameFrame()`.
- **Why:** No waitable timers on Linux. Entry point is `main`. Polling after update (and after sleep) dropped keyboard taps.

### `Sly1-Release/gl.cpp`
- **Edited:** `GL::InitGL()` — `glfwFocusWindow`, `GLFW_STICKY_KEYS`, `GLFW_STICKY_MOUSE_BUTTONS`.
- **Edited:** `MessageBoxA` graphics error only on Windows.
- **Removed (reverted):** `StartupGamepad()` after `glfwInit()`.
- **Why:** Focus + sticky keys make keyboard input reliable. Gamepad mapping load was part of the shelved controller experiment.

### `Sly1-Release/glshaders.cpp`
- **Edited:** `GetEmbeddedShaderSource()` — Win32 still loads RT_RCDATA; Linux reads shader files from `SLY1_SHADER_DIR`, exe dir, or cwd.
- **Why:** No Win32 resources on Linux.

### `Sly1-Release/clock.h`
- **Edited:** types / atomics as needed for a portable clock (no waitable timer in this header).
- **Why:** Frame timing types used by `FramePacer` / `g_clock` on both platforms.

---

## Paths (worlds, saves, FMV)

### `Sly1-Release/transition.cpp`
- **Edited:** `FindWorldsDirectory()` uses `ExecutableDirectory()` instead of `GetModuleFileNameW`.
- **Why:** Locate extracted `Worlds/` next to the binary or in the repo on Linux.

### `Sly1-Release/save.cpp`
- **Edited:** `SaveDirectory()`, `WriteSaveSlot()`, `SaveSystemSettings()` use `ExecutableDirectory()` and `ReplaceFileAtomic()`.
- **Why:** Saves live beside the executable; atomic replace without Win32 `ReplaceFileW`.

### `Sly1-Release/fmv.cpp`
- **Edited:** `CutsceneDirectory()` / `PlayCutscene()` already used `ExecutableDirectory()` for `.m2v` files.
- **Edited:** `PlayCutscene()` — start/stop sidecar WAV via `StartCutsceneAudio` / `StopCutsceneAudio` instead of `#ifdef _WIN32` `PlaySoundW`.
- **Removed:** local `Windows.h` / `mmsystem.h` includes (playback moved into `sound.cpp`).
- **Why:** Windows already played `Cutscene_XX.wav`. Linux had that call compiled out, so movies were silent.

---

## Input (kept) vs controller experiment (reverted)

### `Sly1-Release/Input.h`
- **Edited:** `g_grfjoyt` default usage (definition lives in `Input.cpp`).
- **Removed (reverted):** `StartupGamepad()`, `FillGamepadState()`.
- **Why:** `g_grfjoyt == 0` forced `g_joyZero` and ignored the pad/keyboard stick. Mapping helpers were part of the shelved 8BitDo work.

### `Sly1-Release/Input.cpp`
- **Edited:** `kDefaultKeyboardBindings` — WASD, J/L/K/I, Enter, Backspace, etc.
- **Edited:** `g_grfjoyt` default `7` (camera + move + UI).
- **Edited:** `JOY::Update()` — still only mapped GLFW gamepads (`glfwJoystickIsGamepad` + `glfwGetGamepadState`); keyboard still emulates sticks.
- **Removed (reverted):** `FillGamepadState`, `StartupGamepad`, `LoadGamepadMappingFile`, `LogJoystick`, raw axis/button fallback.
- **Why:** Keyboard had to work. Raw HID axes on unmapped 8BitDo pads caused walking and camera spin; that path was undone.

### `Sly1-Release/prompt.cpp`
- **Edited:** `FGamepadAvailable()` / `GamepadBindingPressed()` stay on `glfwJoystickIsGamepad` / `glfwGetGamepadState`.
- **Removed (reverted):** treating any present joystick as a gamepad and calling `FillGamepadState`.
- **Why:** Same as Input — unmapped devices were not safe to treat as analog sticks.

### `Sly1-Release/gamecontrollerdb.txt`
- **Added then removed.**
- **Why:** SDL mapping DB for 8BitDo. Shelved with the controller bug.

---

## Audio

### `Sly1-Release/sound.h`
- **Added:** `StartCutsceneAudio()`, `StopCutsceneAudio()`.
- **Why:** Shared start/stop for movie WAVs (Win32 `PlaySoundW`, Linux mixer).

### `Sly1-Release/sound.cpp`
- **Edited:** include `xaudio2.h` on Windows, `xaudio2_compat.h` on Linux.
- **Added:** `StartCutsceneAudio()`, `StopCutsceneAudio()`.
- **Edited:** `ShutdownSound()` also calls `StopCutsceneAudio()`.
- **Why:** Game audio already decoded to PCM and submitted to XAudio2. Linux needed a real backend (compat mixer). Movies needed the same WAV path Windows already had.

---

## File Extractor

### `File Extractor/main.cpp`
- **Edited:** `GetExecutableDirectory()` — `/proc/self/exe` on Linux.
- **Edited:** `WaitForExit()` — skip “Press Enter” when stdin is not a TTY.
- **Edited:** `main()` — `argv[1]` ISO path, optional `argv[2]` output directory.
- **Why:** Extract `Worlds/` and `Cutscenes/` on Linux without Win32 APIs or hanging in a non-interactive terminal.

Usage used during this work:

```bash
./build-linux/FileExtractor "Sly Cooper and the Thievius Raccoonus (USA).iso" .
```

---

## Intentionally not done / shelved

| Item | Status |
|------|--------|
| 8BitDo / unmapped HID controllers | Shelved (raw fallback caused ghost analog input) |
| Cutscene audio from the `.m2v` itself | Not needed; Windows uses sidecar `.wav` |
| Rewrite of the 989SND / XAudio2 mixer on Windows | Untouched |

---

## How to build (Linux)

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target Sly1-Release -j$(nproc)
# After extracting the ISO, Worlds/ and Cutscenes/ should sit next to the binary or in the repo root.
./build-linux/Sly1-Release
```
