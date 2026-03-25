# Zoominator Smart Follow & Zoom for OBS Studio

Zoominator is an OBS Studio plugin that lets a capture source smoothly follow the mouse and optionally zoom in, while always keeping the visible area fully covered. It’s built for tutorials, presentations, live coding, and gameplay analysis where the viewer’s attention should naturally track what’s happening on screen.

## Features

### Smart Mouse Follow
The selected source pans to keep the cursor in view with adjustable smoothing.

Supported sources:
- Display Capture
- Window Capture
- Game Capture

### Dynamic Zoom
Set a zoom factor to magnify the area around the cursor.

- **Zoom = 1.0 or 0.0** enables *follow-only mode* (no magnification, just panning).
- Higher values zoom in while preserving full canvas coverage.

### No Corner Leakage
The movement and scaling logic clamps the source so the canvas is always fully filled.  
This works with cropped Window and Game Capture sources as well, preventing black borders from appearing during motion.

### Vertical (Portrait) Canvas Support
When the base canvas is vertical (e.g. 1080×1920), Zoominator can automatically scale the source in “cover” mode so the top and bottom always fill the frame. This is ideal for Shorts, Reels, and other portrait formats.

### Triggers
- Toggle or Hold behavior
- Keyboard or mouse button activation
- Single-key / single-button operation

### Minimal Dock
The dock is intentionally simple:
- Target source selector
- Refresh button

All advanced options are available in **Tools → Zoominator Settings**.

### Persistent Settings
All configuration is saved using OBS’s standard plugin configuration path:

```
%APPDATA%/obs-studio/plugin_config/zoominator/
```

(or the equivalent location on macOS/Linux and in portable mode)

The following are restored on restart:
- Selected source
- Zoom factor
- Follow behavior and speed
- Trigger mode and key/button
- Portrait cover option
- Clamping and movement rules

## Platform Support

### Windows
Full feature set:
- Global mouse tracking
- Keyboard and mouse hooks
- Accurate window and game capture mapping

### macOS and Linux
The zooming and transform logic is cross‑platform, but global input capture is OS‑specific.

- **macOS:** Requires a Quartz event tap and Accessibility permission for global mouse tracking.
- **Linux (X11):** Can be implemented via XInput2.
- **Wayland:** Global cursor tracking is restricted by design; functionality would be limited.

A portable input layer is planned so Zoominator can gracefully adapt to each platform’s capabilities.

## Typical Use Cases

- Software tutorials and live coding
- Product demos
- Strategy and replay analysis
- Esports observing
- Vertical short‑form video production

## Project Notes

Zoominator is designed to behave like a native OBS camera system rather than a script. The core is built on libobs transforms, with a small, focused UI and a configuration system that follows OBS conventions for stability and portability.

## Clang format fix

```bash
git update-index --chmod=+x .github/scripts/build-macos
git update-index --chmod=+x .github/scripts/build-ubuntu
git update-index --chmod=+x .github/scripts/package-macos
git update-index --chmod=+x .github/scripts/package-ubuntu

git update-index --chmod=+x .github/scripts/utils.zsh/check_macos
git update-index --chmod=+x .github/scripts/utils.zsh/check_ubuntu
git update-index --chmod=+x .github/scripts/utils.zsh/log_debug
git update-index --chmod=+x .github/scripts/utils.zsh/log_error
git update-index --chmod=+x .github/scripts/utils.zsh/log_group
git update-index --chmod=+x .github/scripts/utils.zsh/log_info
git update-index --chmod=+x .github/scripts/utils.zsh/log_output
git update-index --chmod=+x .github/scripts/utils.zsh/log_status
git update-index --chmod=+x .github/scripts/utils.zsh/log_warning
git update-index --chmod=+x .github/scripts/utils.zsh/mkcd
git update-index --chmod=+x .github/scripts/utils.zsh/setup_ubuntu
git update-index --chmod=+x .github/scripts/utils.zsh/set_loglevel

git update-index --chmod=+x build-aux/.run-format.zsh
git update-index --chmod=+x build-aux/run-clang-format
git update-index --chmod=+x build-aux/run-gersemi
git update-index --chmod=+x build-aux/run-swift-format

git update-index --chmod=+x build-aux/.functions/log_debug
git update-index --chmod=+x build-aux/.functions/log_error
git update-index --chmod=+x build-aux/.functions/log_group
git update-index --chmod=+x build-aux/.functions/log_info
git update-index --chmod=+x build-aux/.functions/log_output
git update-index --chmod=+x build-aux/.functions/log_status
git update-index --chmod=+x build-aux/.functions/log_warning
git update-index --chmod=+x build-aux/.functions/set_loglevel
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
