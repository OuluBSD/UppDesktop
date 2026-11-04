# OuluBSD/UppDesktop

U++ desktop suite: headless desktop models, multiplatform backend abstraction, X11 manager, taskbar, display manager, and file explorer.

[![UppDesktop CI](https://github.com/OuluBSD/UppDesktop/actions/workflows/ci.yml/badge.svg)](https://github.com/OuluBSD/UppDesktop/actions/workflows/ci.yml)

## Packages

| Package | Purpose |
|---|---|
| `uppsrc/Core/Desktop` | Desktop backend service contract and multiplatform backend abstraction (`Free/`, `Win/`, `Mac/`, `Amiga/`, `Android/`) |
| `uppsrc/DesktopModel` | Headless, presentation-free desktop view models (`WorkspaceModel`, `WindowListModel`, `ScreenInfoModel`, `FrameGeometry`) |
| `uppsrc/DesktopX11` | Multiplatform X11 window, screen, frame, and desktop interaction sources |
| `uppsrc/DesktopManager` | Desktop window management UI, desktop icons, wallpaper, and workspace orchestrator |
| `uppsrc/TaskBar` | Desktop taskbar and dock with window focus cycling and workspace filters |
| `uppsrc/DisplayManager` | Multi-monitor geometry, display arrangement, and work area configuration |
| `uppsrc/DesktopSuite` | Integrated desktop suite coordinating Desktop, TaskBar, Explorer, and ControlPanel |
| `uppsrc/DisplayServer` | Networked and local display server and compositor (`GLMain`, `SoftwareMain`, `Net/`) |
| `uppsrc/Explorer` | High-performance file explorer with headless mode, VFS integration, and TOON UI gate |

## Building & Testing

UppDesktop uses [UppBuilder](https://github.com/OuluBSD/UppBuilder) alongside [BazaarCore](https://github.com/OuluBSD/BazaarCore) and [VFS](https://github.com/OuluBSD/VFS):

```bash
# Build & run headless desktop model unit test
uppbuilder tests/DesktopModelCheck -m CLANG --bazaarcore ../BazaarCore --vfs ../VFS
./bin/DesktopModelCheck

# Build & run desktop services fixture check (XDG / FreeDesktop)
uppbuilder tests/DesktopServicesCheck -m CLANG --bazaarcore ../BazaarCore --vfs ../VFS
./bin/DesktopServicesCheck

# Build & run headless view model probes
uppbuilder tests/DesktopManagerProbe -m CLANG --bazaarcore ../BazaarCore --vfs ../VFS
./bin/DesktopManagerProbe

uppbuilder tests/TaskBarProbe -m CLANG --bazaarcore ../BazaarCore --vfs ../VFS
./bin/TaskBarProbe

uppbuilder tests/DisplayManagerProbe -m CLANG --bazaarcore ../BazaarCore --vfs ../VFS
./bin/DisplayManagerProbe

# Build & run headless File Explorer
uppbuilder uppsrc/Explorer -m CLANG --mainconf HEADLESS --bazaarcore ../BazaarCore --vfs ../VFS
./bin/Explorer /path/to/inspect
```

## License

MIT License. See [LICENSE](LICENSE) for details.
