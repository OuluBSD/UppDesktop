# DisplayManager Package

## Scope
Applies to `uppsrc/DisplayManager` and its entire sub-tree.

## Purpose
- X11 display/screen management for desktop environment
- Screen geometry and resolution management
- Multi-screen support (Xinerama/XRandR)
- EWMH (Extended Window Manager Hints) compliance

## Layout
| File | Role |
|------|------|
| `DisplayManager.upp` | Package manifest |
| `DisplayManager.h` | DisplayManager class declaration; aggregates `X11ScreenSource.h` |
| `DisplayManager.cpp` | DisplayManager implementation: forwards to the model and paints it |
| `main.cpp` | GUI entry point; initializes screens and runs the window |

## Key Components
- `DisplayManager` class - TopWindow-based display manager
- Screen geometry, work area and DPI, all owned by `ScreenInfoModel`
- Work area excludes the panel on top and the taskbar at the bottom
- Screen change handling (`HandleScreenChange`)
- `Paint()` renders per-screen geometry, work area, and DPI as diagnostics

## Fluxbox Reference
Based on `~/Ohjelmat/fluxbox/src/`:
- `Screen.cc/hh` (57628 bytes) - Main screen management
- `ScreenResource.cc/hh` (5978 bytes) - Screen resources
- `ScreenPlacement.cc/hh` (9520 bytes) - Window placement
- `Ewmh.cc/hh` (57314 bytes) - EWMH protocol
- `Xinerama.hh` (4216 bytes) - Multi-screen support
- `Xutil.cc/hh` - X utility functions
- `FbRootWindow.cc/hh` - Root window management
- `RootTheme.cc/hh` - Desktop theme

## EWMH Support
- `_NET_WM_DESKTOP` - Current desktop number
- `_NET_NUMBER_OF_DESKTOPS` - Number of desktops
- `_NET_DESKTOP_GEOMETRY` - Desktop geometry
- `_NET_CURRENT_DESKTOP` - Current desktop
- `_NET_WM_STATE` - Window state
- `_NET_CLIENT_LIST` - List of windows

## Integration
- Provides screen info to DesktopManager and TaskBar through `ScreenInfoModel`
- Reads Xlib through `X11ScreenSource` alone; the view no longer uses `Xdisplay`,
  `Xroot` or `Xscreenno`
- Handles XRandR/Xinerama events

## Model / view split
Frozen by task DM2-004. Rules and rationale: `ai/info/desktop-model-view-split.md`.
The owning class is `ScreenInfoModel` from the new `uppsrc/DesktopModel` package,
reading through an injected source.

| Member | Now | Owner |
|--------|-----|-------|
| ctor / dtor | view | stays a `TopWindow` subclass. |
| `Initialize()` | view | constructs nothing itself: `X11ScreenSource` is a member and `ScreenInfoModel::Update()` pulls through it. |
| `GetScreenCount()` | forward | `ScreenInfoModel::GetCount()`. |
| `GetScreenGeometry(i)` | forward | `ScreenInfoModel::Geometry(i)`. |
| `GetWorkArea(i)` | forward | `ScreenInfoModel::WorkArea(i)`. |
| `GetDPI(i)`, `GetDPI(i, h, v)` | forward | `ScreenInfoModel::DPI()`. The single-value form is a view convenience, not a second source of truth. |
| `HandleScreenChange(i)` | forward | `ScreenInfoModel::RefreshScreen(i)`. The XRandR / Xinerama event wiring that would call it stays view-side. |
| `SetTaskbarHeight(h)`, `SetPanelHeight(h)`, both getters | forward | `ScreenInfoModel`. The model recomputes the work area itself, so the setters are no longer its only callers. |
| `GetModel()` | view | hands the model to callers that want the state itself. |
| `Paint()` | view | reads the model and draws the diagnostics. It mutates nothing. |
| `ReadScreen(i)` | gone | `X11ScreenSource::ReadScreen(i, out)`. |
| `RecalcWorkAreas()` | gone | `ScreenInfoModel::RecalcWorkAreas()`. |
| `RecalcDPI()` | gone | dropped. It was dead code sitting behind a nested preprocessor branch, and `Update()` already re-reads every screen. |
| `screen_geometries`, `work_areas`, `dpi_x`, `dpi_y` | gone | one `ScreenRecord` per screen inside the model. The four parallel vectors were what let the work areas run ahead of the geometries when a screen count changed. |

## The X11 screen source
`X11ScreenSource` no longer lives here. It moved to `uppsrc/DesktopX11` in
DM4-005, because this package is a MAIN package and a check package could not
link a source living in one: the collision on `main` meant the source's
dead-server path had to be verified by hand, by temporarily rewriting this
package's `main.cpp` into console mode and putting it back afterwards.
`upptst/ScreenSourceCheck` now covers those cases as ordinary checks.

- It calls `XOpenDisplay()` itself. The global `Xdisplay` from
  `uppsrc/CtrlCore/X11Gui.h:30` is unreachable from a translation unit that does
  not include `CtrlCore`, so a source built on it could not be reused headlessly.
- It opens and closes per query, not per object. There is therefore no connection
  to go stale: a server that disappears makes `GetCount()` report 0 and
  `ReadScreen()` report false, which leaves the model empty instead of handing
  back geometry read from a dead socket. It also means there is no `Display*` to
  leak.
- It reports raw pixels and millimetres. `ScreenInfoModel` derives DPI, so a
  source stays a source.
- The count method is `GetCount()`, not `ScreenCount()`. `GetCount()` matches
  `Vector::GetCount()` and the rest of the codebase, so writing `Get` and letting
  Assist complete it lands here; `ScreenCount()` is impossible anyway, because
  `X11/Xlib.h` defines it as a macro and a macro cannot be dodged by
  qualification. Do not "fix" that name back.

Verified against `xdpyinfo` on the same server (DM2-017):

| | Xvfb `:95` | `xdpyinfo` |
|---|---|---|
| geometry | 1280 x 800 at 0,0 | dimensions 1280x800 pixels |
| DPI | 100 x 100 | resolution 100x100 dots per inch, 325x203 mm |
| work area, defaults | 1280 x 776 at 0,0 | panel 0 + taskbar 24 subtracted |
| work area, taskbar 40 panel 24 | 1280 x 736 at 0,24 | same arithmetic |

## Mainconfigs
```
bin/build -m CLANG -M X11 <Package>           # X11 build
bin/build -m CLANG -M "X11 (Valgrind)" <Package>
```

Build all three desktop packages in one command with per-package pass or fail
and a non-zero exit on any failure:

```
script/build_desktop_packages.sh
```

### The X11 flag is required, not optional
Both mainconfigs pass `X11`. This is **load-bearing**:

- The build system turns the `X11` keyword into `flagX11`.
- `uppsrc/CtrlCore/CtrlCore.h` selects `X11Gui.h` only `#ifdef flagX11`,
  and `Gtk.h` otherwise.
- Every `#if defined flagPOSIX && defined flagX11` block in this package used to
  compile to nothing -- silently, with no error, no warning, and no log -- or
  to a set of stub methods that reported zero screens and returned false.
  `DisplayManager.cpp` now carries an `#error` instead, and those stubs are
  gone.

This package was previously built as plain `"GUI"`, which defined neither
`flagX11` nor `flagNOGTK`. It compiled cleanly and opened a window while
containing no X11 code at all: `DisplayManager` reported zero screens, and
`DesktopManager::LowerWindow()` had been reduced to an empty function.

Use `X11`, not `NOGTK`. `NOGTK` happens to imply `flagX11` and so also works,
but `X11` is the established repo flag (`Adventure`, `CardBoardEditor`,
`MluiViewer`, `NodeWorkbench` all use `"GUI X11"`); `NOGTK` appears only in
`ScriptIDE` and `GameLauncher`. Both select the same backend.

If you add a mainconfig, keep `X11`. A build without it no longer compiles:
the missing flag is a hard error, not a quietly X11-free binary that reports
zero screens.

GUI-only, and it stays that way: the view logic lives in `Ctrl`- or
`TopWindow`-derived classes and `CtrlCore` refuses to compile without the GUI
flag. Do not add a no-flag mainconfig here. Adding one would compile both `main`
functions into one binary and collide at link time, which is why the headless
twin is a separate package (`upptst/DisplayManagerProbe`) rather than a second
mainconfig. See `ai/rules/headless-gui-dual-purpose.md` and Known Gaps.

## Known Gaps
- The headless twin is `upptst/DisplayManagerProbe` (DM2-011). It drives
  `ScreenInfoModel` and its work-area arithmetic against a fake source. The real
  source has its own check, `upptst/ScreenSourceCheck`, which it can link only
  because the source moved to `uppsrc/DesktopX11` in DM4-005. Between the two,
  every path that used to need a temporary console `main` is covered: a live
  server, a dead display, an unset `DISPLAY`, out-of-range indices, the DPI
  fallback, and 100 consecutive queries to show the per-query connection does not
  accumulate anything. Do not put a second `main` back into this package.
- A GUI binary cannot observe "no X server" through this class. `Ctrl::InitX11`
  panics on a dead display before any view code runs, so the zero-screen path is
  only reachable from a caller that does not initialise Ctrl.
- **No resolution setting.** The old `SetResolution()` only rewrote a cached
  `Rect` and returned `true`, so it reported success while nothing happened.
  It was removed rather than left as a lie. Real mode setting needs XRandR
  (`XRRSetScreenSizeAndRate`); `libXrandr.so` is present on this machine but
  the package does not link it, because adding an external dependency to the
  manifest is a deliberate decision. Until then, expose only read-only info.
- `HandleScreenChange()` re-reads the X screen on demand but nothing calls it.
  There is no XRandR `RRScreenChangeNotify` / Xinerama event wiring, so a
  hot-plugged monitor is not noticed.
- DPI falls back to 0 ("unknown") when the server reports 0 mm, which is what
  many VMs do. `Paint()` labels that case explicitly rather than showing 0.
- Screen enumeration only covers the classic X11 `Screen` list. A single
  physical display split into several `Screen`s, and true multi-monitor
  layouts that need Xinerama frame merging, are not handled — the advertised
  Xinerama support is not implemented.
- EWMH properties are listed below as design intent only; none are set.
