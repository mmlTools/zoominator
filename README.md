# Zoominator – Smart Scene Zoom & Follow for OBS Studio

[![Watch the demo](https://img.youtube.com/vi/R3KQnsnzIAM/0.jpg)](https://www.youtube.com/watch?v=R3KQnsnzIAM)

Zoominator is an OBS Studio plugin that dynamically **zooms and pans the entire scene** to follow your mouse, keeping the focus exactly where the action happens.

It operates at the **scene level**, meaning it works with any source automatically no per-source setup, no dock, no complexity.

---

## What It Does

- **Scene-wide Zoom & Pan**  
  Smoothly transforms the entire scene based on mouse position.

- **Smart Clamping**  
  Ensures the canvas is always fully covered no black edges, even with cropped sources.

- **Mouse-Driven Focus**  
  Keeps attention exactly where your cursor is.

- **Click Highlight (Halo)**  
  Optional visual feedback on mouse clicks using a configurable halo.

- **Flexible Activation**  
  Toggle or hold behavior with customizable key combinations.

---

## Installation

### Windows
1. Download the latest release
2. Extract the archive and move the zoominator.dll file into your OBS Studio directory:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit
   ```
3. Restart OBS

### macOS
1. Download the `.pkg` or `.dmg` from releases
2. Install and restart OBS

### Linux (X11)
1. Build from source or install via package (if available)
2. Copy plugin files into:
   ```
   ~/.config/obs-studio/plugins/
   ```
3. Restart OBS

---

## Build from Source

### Requirements
- OBS Studio development libraries
- CMake (3.20+ recommended)
- C++17 compatible compiler
- Qt6

### Steps
```bash
git clone https://github.com/your-repo/zoominator.git
cd zoominator
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## Compatibility Notes

- **Windows:** Full support (global input + smooth tracking)
- **macOS:** Requires Accessibility permissions for input tracking
- **Linux (X11):** Supported via XInput2
- **Wayland:** Limited (no global cursor tracking)

---

## Use Cases

- Tutorials & live coding  
- Product demos  
- Gameplay & analysis  
- Vertical / short-form content  

---

## Notes

Zoominator behaves like a **virtual camera system inside OBS**, applying transformations at the scene level for maximum flexibility and reliability.

## Clang format fix

```bash
git add --chmod=+x build-aux/.run-format.zsh build-aux/run-clang-format build-aux/run-gersemi build-aux/run-swift-format build-aux/.functions/*
git add --renormalize .
git commit -m "Make build scripts executable and normalize line endings"
```

```gitattributes
.gitattributes
*.sh text eol=lf
*.zsh text eol=lf
.github/scripts/** text eol=lf
.functions/* text eol=lf
run-clang-format text eol=lf
run-gersemi text eol=lf
run-swift-format text eol=lf
.run-format.zsh text eol=lf
```

```bash
git add --renormalize .
git commit -m "Normalize line endings for Unix scripts"
git push
```
