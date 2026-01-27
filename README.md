# Zoominator – Smart Follow & Zoom for OBS Studio

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
