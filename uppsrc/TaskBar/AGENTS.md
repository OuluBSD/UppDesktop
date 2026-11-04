# TaskBar Package

## Scope
Applies to `uppsrc/TaskBar` and its entire sub-tree.

## Purpose
- Application taskbar for X11 desktop environment
- Running application list with window switching
- System tray area
- Quick launch / application launcher

## Layout
| File | Role |
|------|------|
| `TaskBar.upp` | Package manifest |
| `TaskBar.h` | TaskBar class declaration |
| `TaskBar.cpp` | TaskBar implementation |
| `main.cpp` | GUI entry point; hosts TaskBar in a frameless bottom-edge window |

## Key Components
- `TaskBar` class - Ctrl-based taskbar widget
- Window item tracking (WinItem struct)
- Application list with titles and active states
- Click-to-focus window switching
- `TaskBar::CycleWindows()` / `TaskBar::CycleAllTopWindows()` for Alt+Tab style cycling

## Fluxbox Reference
Based on `~/Ohjelmat/fluxbox/src/`:
- `Toolbar.cc/hh` (37605 bytes) - Main toolbar implementation
- `IconbarTool.cc/hh` (21591 bytes) - Icon bar with launchers
- `SystemTray.cc/hh` (22001 bytes) - System tray
- `ClockTool.cc/hh` (11289 bytes) - Clock/status display
- `GenericTool.cc/hh` - Base toolbar tool
- `ToolbarItem.cc/hh` - Individual toolbar items
- `ToolbarTheme.cc/hh` - Theme management

## Implementation Notes
- `TaskBar` is a plain `Ctrl`, so it needs a host window. `main.cpp` supplies a
  frameless `TopWindow` pinned to the bottom of `Ctrl::GetVirtualScreenArea()`.
- Reads `X11WindowSource` (package `uppsrc/DesktopX11`) to track windows, so the
  list covers every window the X server manages rather than only the ones this
  process created. This process's own windows are filtered out by
  `IsOwnWindow()`
- `Sync()` drains the watch connection and refreshes the window list only when the
  X server has reported a change. It runs from a 500 ms `SetTimeCallback` (and
  after a click), not from `Paint()`
- `Layout()` calculates item positions; also re-run on a timer because window
  geometries shift as windows open and close
- `Paint()` only draws; it must not mutate the item list
- `CycleWindows()` cycles the tracked list; `CycleAllTopWindows()` is the
  static form for a global shortcut and falls back to asking the source directly
  when no taskbar instance is live

### U++ API notes for this package
- `THISBACK` expands to `&CLASSNAME::x`, and `CLASSNAME` is a private member of
  `Ctrl`, so a `Ctrl` subclass cannot use `THISBACK`. Bind a lambda instead.
- `Vector::Remove` takes an **index**, not a value. There is no `Vector::Find`;
  linear membership tests are written out.
- `Ctrl::GetFocusCtrl()` returns `Nuller`, which will not bind to `Ctrl*`.
  Locate the focused window with `IsForeground()` over `Ctrl::GetTopCtrls()`.

## Integration
- Works with DesktopManager for window focus/activation
- Can run independently or as part of desktop suite
- X11 integration via CtrlCore backend

## Model / view split
Frozen by task DM2-004. Rules and rationale: `ai/info/desktop-model-view-split.md`.
The owning class is `WindowListModel` from the new `uppsrc/DesktopModel` package,
shared with `DesktopManager`.

| Member | Now | Owner |
|--------|-----|-------|
| ctor / dtor | view | stays a `Ctrl` subclass with the two `SetTimeCallback` timers and the `live` registry. |
| `Paint()` | view | reads the model for the titles and the active flag, and its own `rects` for the geometry. Writes nothing. |
| `Layout()` | view | owns `rects`, one per listed entry. Item geometry is presentation. |
| `Sync()` | forward | `source.Poll()` first, and returns immediately when the server has reported no change. Only then does `SnapshotWindows()` ask the server, `SetWorkspace(source.CurrentDesktop())` names the workspace the panel answers for, and `WindowListModel::Sync()` owns the filter, the add, the drop, the title snapshot and the active flag; `Layout()` and `Refresh()` run as before. |
| `LeftDown()` | forward | hit-tests its own rect, asks the model for the handle at that index, and raises and focuses it through the source, then re-syncs. Every listed window belongs to another process, so there is no `TopWindow*` to activate. |
| `CycleWindows()` | forward | `WindowListModel::NextIndex(1)`, then raise and focus through the source. The old `active_idx = -1` search and the `%` are gone. |
| `CycleAllTopWindows()` | view | the `live` registry and the `live[0]` limitation stay, because they are about which taskbar answers rather than what the list means. The body changed: it used to build a `Vector<TopWindow*>` and raise each, which is impossible now, so it takes the next handle from a model and raises it through **the source that filled that model**. Handles are interned per source instance, so raising a live taskbar's handle through a different source would silently do nothing. The fallback keeps its own source and its own model together for that reason. The `%` and the foreground scan are gone; `NextFocus(1)` is the same answer. |
| `GetItemCount()` / `GetItemTitle(i)` | forward | `WindowListModel::GetCount()` / `TitleAt(i)`. |
| `WinItem` | gone | the handle, title and active flag are `WindowEntry` in the model; the `Rect` is in `Vector<Rect> rects`; the `Ptr<TopWindow>` is in the handle table. |
| `items` | gone | `WindowListModel windows`, plus `rects` for geometry. |
| `CollectWindows()` | view | `Ctrl::GetTopCtrls()` and the host-window filter. A GUI concern, so it stays here. |
| `SnapshotWindows()` / `HandleOf()` / `Resolve()` | gone | `X11WindowSource::ReadWindows()` interns the handles and resolves both ways, so this package holds no handle table and needs no `next_handle`. |
| `CollectWindows()` | gone | the source reads the server; `IsOwnWindow()` drops this process's windows. |
| `IsOwnWindow()` | view | new. The host window first, since a taskbar with a button for itself is this file's oldest bug, then every window in `Ctrl::GetTopCtrls()`. |
| `handle_keys`, `handle_values`, `next_handle` | gone | handle identity and lifetime belong to the source. |
| `static live` | view | unchanged. |

## What DM5-003 changed
The panel answers for the workspace the manager reports as current, not for all
of them. `Sync()` reads `source.CurrentDesktop()` (a per-query read inside the
`ReadWindows()` call `SnapshotWindows()` already made) and hands it to
`SetWorkspace()`; the filter itself, the sticky pass-through and the resort
after a switch are the model's, decided by the frozen contract's
"Per-workspace window sets". On a bare server with no `_NET_CURRENT_DESKTOP`
the source reports `AllDesktops` and the panel lists everything, exactly as
before this task.

A switch arrives through the existing watch: `Watch()` already selects
`PropertyChangeMask` on the root, so the root property change marks the
connection dirty and the next tick syncs with the new workspace.

Verified live on Xephyr `:96` (no window manager, so the test published
`_NET_WM_DESKTOP` on the windows and `_NET_CURRENT_DESKTOP` on the root with
`xprop -f ... 32c -set ...` — the same publication the live checks automate):
a window naming workspace 1 dropped from the panel while the root named 0,
reappeared on a switch to 1, disappeared again on the switch back, and the
sticky window was counted in both.

## What DM4-004 changed, and what it did not
The window list is no longer re-read on every tick. `source.Watch()` selects
`SubstructureNotify` on the root at construction, `Sync()` drains it, and a tick
with nothing having happened does no window queries at all -- one non-blocking
`XPending()` and nothing more.

The tick itself is still there. CtrlCore selects no root event mask and exposes no
hook, so an event has nothing to call; `Paint()` cannot be the answer either,
because a repaint does not follow another client opening a window. Verified on
Xvfb `:95`: `TaskBar` reported 0 items, then 1 within a second of `xmessage`
appearing, then 0 again within a second of it closing, with no re-read in between.
What went away is the *rebuild*, not the timer.

## One expected behaviour change
With exactly one listed window the model returns it in **both** directions, where
the old `if(items.GetCount() <= 1) return;` did nothing. Pressing the button of
the only application on screen now focuses that application instead of doing
nothing. Covered by `upptst/TaskBarProbe`.

The `(untitled)` fallback for an empty title happens in `X11WindowSource`, not in
the model: a model must never invent text it was not given, and both views paint
the same fallback, so it is decided once in the source rather than twice in the
views. It used to be `Window` here and `(untitled)` in `DesktopManager`.

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
  compile to nothing -- silently, with no error, no warning, and no log.
  `TaskBar.cpp` now carries an `#error` instead, even though it calls no Xlib
  function itself, because the flag also picks the backend CtrlCore links.

This package was previously built as plain `"GUI"`, which defined neither
`flagX11` nor `flagNOGTK`. It compiled cleanly and opened a window while
containing no X11 code at all: `DisplayManager` reported zero screens, and
`DesktopManager::LowerWindow()` had been reduced to an empty function.

Use `X11`, not `NOGTK`. `NOGTK` happens to imply `flagX11` and so also works,
but `X11` is the established repo flag (`Adventure`, `CardBoardEditor`,
`MluiViewer`, `NodeWorkbench` all use `"GUI X11"`); `NOGTK` appears only in
`ScriptIDE` and `GameLauncher`. Both select the same backend.

If you add a mainconfig, keep `X11`. A build without it no longer compiles:
the missing flag is a hard error, not a quietly GTK-linked binary.

GUI-only, and it stays that way: the view logic lives in `Ctrl`- or
`TopWindow`-derived classes and `CtrlCore` refuses to compile without the GUI
flag. Do not add a no-flag mainconfig here; two `main` functions in one binary
collide at link time. The headless twin is `upptst/TaskBarProbe`, which uses
`Core` and `DesktopModel` and never this package.

## Known Gaps
- The headless twin is `upptst/TaskBarProbe` (DM2-011). It drives the sync, the
  click and the cycling the view drives, and prints the list after each step. It
  cannot link this package, because this package is a MAIN package and its
  `main` would collide.
- The 500 ms tick remains and is documented above. Removing it needs a hook in
  `CtrlCore` for events on the root, which is a change to a package every GUI in
  the repo includes.
- `TIMER_LAYOUT` also runs every 500 ms and re-runs `Layout(); Refresh()`. It was
  there to follow window geometry changes, which the geometry it lays out -- its
  own items -- no longer depends on. It is a candidate for deletion with the
  tick.
- `CycleAllTopWindows()` uses `live[0]`, the first registered taskbar, so with
  more than one taskbar on screen it cycles only the first one's list.
- With no taskbar instance live, `CycleAllTopWindows()` does not filter this
  process's own windows the way `SnapshotWindows()` does, because there is no
  `TaskBar` object to ask. It will include the desktop's own windows. Filtering
  there needs the shared filter that `IsOwnWindow()` should grow into; see
  DM4-004.
- No clock, system tray, or launcher, despite the package Purpose listing them.
  Only the window list and click-to-raise are implemented.
