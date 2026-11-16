# Flatpak packaging

These files build Zoominator as a plugin extension for the OBS Studio Flatpak
(`com.obsproject.Studio` from Flathub).

| File | Purpose |
| --- | --- |
| `com.obsproject.Studio.Plugin.Zoominator.json` | `flatpak-builder` manifest. Builds the checkout it sits in. |
| `com.obsproject.Studio.Plugin.Zoominator.metainfo.xml` | AppStream metadata shown by software centers. |
| `flathub.json` | Flathub build options (x86_64 only, like OBS itself). |

## Build and install

You need `flatpak`, `flatpak-builder` and the Flathub remote. From the repository root:

```bash
flatpak-builder --user --install --install-deps-from=flathub --force-clean \
  build_flatpak build-aux/flatpak/com.obsproject.Studio.Plugin.Zoominator.json
```

This downloads OBS Studio and the SDK if they are missing, builds the plugin and installs
it for the current user. Restart OBS; Zoominator is then in the `Tools` menu.

To hand the build to someone else as a single file:

```bash
flatpak-builder --user --install-deps-from=flathub --force-clean --repo=repo \
  build_flatpak build-aux/flatpak/com.obsproject.Studio.Plugin.Zoominator.json
flatpak build-bundle --runtime repo zoominator.flatpak \
  com.obsproject.Studio.Plugin.Zoominator stable
flatpak install --user zoominator.flatpak
```

To remove it: `flatpak uninstall com.obsproject.Studio.Plugin.Zoominator`.

## Limits

Zoominator reads global input and the cursor position through XInput2, so OBS has to run
in an **X11 session**. The OBS Flatpak uses Wayland whenever it is available, and in a
native Wayland session the plugin loads but its triggers and mouse tracking are disabled.

## Maintaining

- **SDK version.** `sdk` in the manifest must be the SDK of the runtime the OBS Flatpak is
  built on: see `runtime-version` in
  [`build-aux/com.obsproject.Studio.json`](https://github.com/obsproject/obs-studio/blob/master/build-aux/com.obsproject.Studio.json)
  of the OBS release being targeted (OBS 33 uses `org.freedesktop.Sdk` 26.08).
- **Releases.** Add a `<release>` line to the metainfo file for every tagged version.

## Publishing on Flathub

Flathub builds from its own repository, one per plugin, created through a pull request to
[flathub/flathub](https://github.com/flathub/flathub); see the
[submission guide](https://docs.flathub.org/docs/for-app-authors/submission). Submit the
three files above, with the `dir` source in the manifest replaced by the release tag:

```json
{
    "type": "git",
    "url": "https://github.com/mmlTools/zoominator.git",
    "tag": "<version>",
    "commit": "<commit the tag points at>"
}
```
