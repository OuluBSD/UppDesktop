# DesktopManager Package

## Scope
Applies to `uppsrc/DesktopManager` and its entire sub-tree.

## Purpose
- X11 desktop window manager based on fluxbox architecture
- Virtual desktop/workspace management
- Window placement, focus, and stacking
- Panel and launcher support
- Integration with Explorer big icon view

## Layout
| File | Role |
|------|------|
| `DesktopManager.upp` | Package manifest |
| `DesktopManager.h` | Main header with DesktopManager class |
| `DesktopManager.cpp` | Implementation |
| `DesktopManagerIcons.h` | Icon definitions |
| `main.cpp` | GUI entry point; initializes the desktop and runs the window |

## Key Components
- `DesktopManager` class - TopWindow-based desktop manager
- Workspace management (create, remove, switch)
- Window stacking and focus control
- `Paint()` renders the workspace index and the open-window list
- Integration points for TaskBar and DisplayManager

## Fluxbox Reference
Based on `~/Ohjelmat/fluxbox/src/`:
- `Screen.cc/hh` - BScreen class for screen/workspace management
- `Workspace.cc/hh` - Virtual desktop implementation
- `Window.cc/hh` - Window management
- `Slit.cc/hh` - Panel/bar management

## Model / view split
Frozen by task DM2-004. Rules and rationale: `ai/info/desktop-model-view-split.md`.
Owning classes named below live in the new `uppsrc/DesktopModel` package, which
`uses Core` only. Do not move a method before reading that file.

| Member | Now | Owner |
|--------|-----|-------|
| ctor / dtor | view | stays a `TopWindow` subclass, plus the handle counter. |
| `Initialize()` | view | creates the root window and then calls `CreateWorkspace()`, which asks the model for an index. |
| `CreateWorkspace()` | forward | `WorkspaceModel::CreateWorkspace()` returns the index; the view opens the window for it. |
| `RemoveWorkspace()` | forward | `WorkspaceModel::RemoveWorkspace(Current())`. The view must not clamp: the model clamps after the removal, and a pre-clamp is the defect DM2-005 fixed. It then opens the window for the new current index, which `One<TopWindow>` left closed. |
| `NextWorkspace()` / `PrevWorkspace()` | forward | `WorkspaceModel::SwitchToRelative(+1 / -1)`. No wrapping, as before. |
| `SetCurrentWorkspace(id)` | forward | `WorkspaceModel::SwitchTo(id)`, then `ApplySwitch()` closes `from` and opens `to`. Neither index is recomputed in the view. |
| `GetCurrentWorkspace()` / `GetWorkspaceCount()` | forward | `WorkspaceModel::Current()` / `GetCount()`. |
| `FocusNext()` / `FocusPrev()` | forward | `WindowListModel::NextFocus()`. The old index arithmetic, the `-1` start and the modulo are gone from this package. |
| `RemoveWorkspace` clamping | fixed | `Current()` is always a valid index, including after removing the workspace that was current and last. |
| `RaiseWindow(win)` / `LowerWindow(win)` | view | unchanged. `LowerWindow()` is still the only direct Xlib call here. |
| `GetRoot()` / `GetWorkspace(id)` | view | unchanged handle lookups. |
| `Paint()` | view | syncs, then reads the model and draws. |
| `CycleFocus(step)` | view | syncs, asks the model for a handle, resolves it, calls `SetForeground()`. |
| `SyncWindows()` | view | `SnapshotWindows()` asks `X11WindowSource` and drops this process's own windows, `SetWorkspace(source.CurrentDesktop())` names the workspace the desktop answers for, and `WindowListModel::Sync()` owns the filter, the add/drop/title/focus bookkeeping. |
| `WindowsChanged()` | view | drains the watch connection. `Paint()` calls it and only re-reads when it reports a change, so a repaint with nothing having happened costs no window queries. |
| `SnapshotWindows()` | view | asks `X11WindowSource` and drops this process's own windows. The `(untitled)` fallback now happens in the source, because both views paint the same text and it should be decided once. |
| `current_workspace`, `Vector<One<TopWindow>> workspaces` as state | split | the vector is still there, because a `TopWindow` per workspace is a GUI thing. The count and the current index moved to the model. |
| `HandleOf()` / `Resolve()` | gone | `X11WindowSource::HandleOf()` / `NativeOf()`. The source interns handles, so the view holds no table. |
| `handle_keys`, `handle_values`, `PruneHandles()`, `FirstHandle` | gone | the source owns handle identity and its lifetime. |
| `CollectOpenTopWindows()` | gone | `X11WindowSource::ReadWindows()`. |
| `IsOwnWindow()` | view | new. Filters this process's windows out of the server's list. |

## What DM5-004 changed
The desktop window's list answers for the workspace the manager reports as
current, the same one-call wiring as the taskbar (DM5-003): `SyncWindows()`
holds the snapshot from `SnapshotWindows()` -- whose read also refreshed
`CurrentDesktop()` -- hands it to `SetWorkspace()`, then syncs. The filter,
the sticky pass-through and the post-switch resort are the model's
(DM5-002). `CycleFocus()` inherits the filter through `SyncWindows()`, so
Alt-tab-style cycling never lands on a window of another workspace.

This package has no timer, so it learns of a switch only at the next repaint.
The watch marks the connection dirty on the root property change; the Paint
that follows any exposure does the sync. Verified on Xephyr `:96` with no
window manager: `xprop` published `_NET_WM_DESKTOP` on two `xmessage`
windows (0 and 1) and `_NET_CURRENT_DESKTOP` on the root, and the desktop's
painted list swapped the two windows across the switch in both directions
(read back with tesseract over crops of the root capture -- no visual
inspection was available in the session). Sticky windows, own trio and the
other processes' windows were listed in every state.

Two live-session traps worth remembering, both about who is above whom:
`xwininfo -root -children` lists **top to bottom** in this tree, so a window
late in that list slides under the desktop and sweeping it exposes nothing;
and the `DisplayManager` window at `+40+60` covered the desktop's list area,
so the first crops OCR'd the wrong surface. Expose the list region with a
window that is actually above it.

## Two things the rewire had to decide
- **Where the sync happens.** This package has no polling timer, and before the
  split `Paint()` and `CycleFocus()` each asked `Ctrl` for the open windows
  themselves. Syncing at both reads keeps the list exactly as current as it was
  and keeps the model out of `Paint()`'s logic. `TaskBar` does have a timer and
  syncs from it instead.
- **Handles start at 1.** `WindowListModel::NoHandle` is 0 and must never be a
  live handle, so model space cannot start at 0. The table is pruned on every
  sync so it stays proportional to the open windows.

## The window list is system-wide (DM4-003)
`SyncWindows()` reads `X11WindowSource`, which asks the X server for every window
it manages, so a window opened by another program now appears in the model and
takes part in focus cycling. Verified on Xvfb `:95` with `xmessage` as an
unrelated client: the model held exactly that one window, and the three windows
this process creates were all filtered out.

The handle table this package used to keep is gone. `handle_keys`,
`handle_values`, `HandleOf()`, `Resolve()`, `PruneHandles()` and `FirstHandle`
were all there because the model cannot name a `TopWindow`, and the view had to
translate between the two. The source interns a handle per native window and
resolves in both directions, so the view asks it instead.

`Ctrl::GetTopCtrls()` survives in exactly one place, `IsOwnWindow()`, and only to
decide what to *leave out*. That is not a leftover of the old design: a window
carries no "who made me", so the server cannot answer whether a window is ours,
and the process-local API is the only thing that can. The list itself comes from
the server.

### Excluding our own windows
`IsOwnWindow()` compares `TopWindow::GetWindow()` against the root window, every
workspace window, and every window in `Ctrl::GetTopCtrls()`. The last part is
needed: the `DesktopManager` window itself and a 1x1 helper window CtrlCore opens
are not members of this class, and without them the taskbar lists the desktop.

### Why the rewire reads the server but still filters locally
`SyncWindows()` opens a connection and walks the tree, which is far more work than
the old `Ctrl::GetTopCtrls()` did, so doing it unconditionally on every paint
would have been a regression. It is not done unconditionally: the constructor
calls `source.Watch()`, `Paint()` calls `WindowsChanged()`, and the read happens
only when the X server has reported a change. See "What DM4-004 changed" in
`uppsrc/TaskBar/AGENTS.md`, which covers the shared behaviour.

### Titles arrive one sync late, and that is fine
The first `Paint()` can read `Workspace 1` and `DesktopManager` as `(untitled)`
and the next sync reads them correctly. `uppsrc/CtrlCore/X11Top.cpp:94` writes the
X title only `if(IsOpen() && GetWindow())`, so a title set before the window opens
reaches the server later. The model refreshes every title on every sync, so it
settles; observed across three syncs rather than assumed.

## Mainconfigs
```
bin/build -m CLANG -M X11 <Package>           # X11 build
bin/build -m CLANG -M "X11 (Manager)" <Package>   # manager role (opt-in, DM6-002)
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
  `DesktopManager.cpp` now carries an `#error` instead, so that mistake stops
  the build.

This package was previously built as plain `"GUI"`, which defined neither
`flagX11` nor `flagNOGTK`. It compiled cleanly and opened a window while
containing no X11 code at all: `DisplayManager` reported zero screens, and
`DesktopManager::LowerWindow()` had been reduced to an empty function.

Use `X11`, not `NOGTK`. `NOGTK` happens to imply `flagX11` and so also works,
but `X11` is the established repo flag (`Adventure`, `CardBoardEditor`,
`MluiViewer`, `NodeWorkbench` all use `"GUI X11"`); `NOGTK` appears only in
`ScriptIDE` and `GameLauncher`. Both select the same backend.

If you add a mainconfig, keep `X11`. A build without it no longer compiles:
the missing flag is a hard error, not a quietly X11-free binary.

GUI-only, and it stays that way: the view logic lives in `Ctrl`- or
`TopWindow`-derived classes and `CtrlCore` refuses to compile without the GUI
flag. Do not add a no-flag mainconfig here; two `main` functions in one binary
collide at link time. The headless twin is `upptst/DesktopManagerProbe`, which
uses `Core` and `DesktopModel` and never this package.

## Known Gaps
- **Manager role is opt-in (DM6-002).** Only a DesktopManager process started with the WMANAGER mainconfig flag constructs an X11WindowManager member and attempts takeover of the root window. If another manager is already running, Takeover() returns false and the process continues as a plain desktop — exactly as it would have without the flag. The X11WindowManager handles MapRequest, ConfigureRequest, CirculateRequest, etc. from a dedicated connection and never interferes with view connections.
- **The manager queue is serviced by the manager's own thread (fixes the deadlock the DM6-003 live run found).** The first wiring drained the request loop from `WindowsChanged()`, i.e. only from `Paint()`, and that could never work: CtrlCore's `TopWindow::Open()` ends with `XWindowEvent(Xdisplay, xwin, VisibilityChangeMask, &e)` (`uppsrc/CtrlCore/X11Top.cpp:328`), a blocking Xlib wait for the window to become visible, and while `X11WindowManager` holds `SubstructureRedirect` that event only arrives once the manager answers the window's `MapRequest` — from the same main thread, which is inside the blocking `XWindowEvent` and cannot return. The desktop's first window never mapped and the role held the whole display hostage; a Ctrl timer cannot fix it, because the main thread is blocked outside Ctrl's loop. The fix is in the manager: `X11WindowManager` services its connection from its own thread (`EventLoop()`), which `Takeover()` starts and whose shutdown is a `ClientMessage` to an internal 1x1 wake window, because `XNextEvent` has no timeout. This is safe because CtrlCore calls `XInitThreads()` before opening anything (`uppsrc/CtrlCore/X11App.cpp:406`) and the worker touches only its own connection; `ProcessRequest()` refuses while the worker owns the queue, so a second reader cannot race it. `WindowsChanged()` still calls `ProcessRequest()` as the fallback drain for the plain-desktop case, where the manager took nothing over, but that is a no-op in the manager role.
- **Own-window exclusion in the manager is by window class first, ids second.** Desktop and workspace windows are opened with `SubstructureRedirect` already in force, so a `MapRequest` for each of them arrives at the manager before the view has any chance to call `SetOwnWindows()`. `X11WindowManager::IsOwnWindow()` therefore also matches the `WM_CLASS` CtrlCore stamps on everything it opens (`res_name` and `res_class` are both `GetExeTitle()`, `uppsrc/CtrlCore/X11Top.cpp:317-321`), which is true at the moment the map request is served. The id list from `SetOwnWindows()` (`SyncWindows()` calls it every sync) is the belt to that suspenders.
- `CycleFocus()` now raises and focuses through `X11WindowSource`, which sends
  `_NET_ACTIVE_WINDOW` as a pager request rather than calling `XSetInputFocus`.
  On a bare X server with no window manager that request goes nowhere, so focus
  cycling cannot be observed end to end there. With a real window manager it is
  the correct request, and `XSetInputFocus` would have fought the window manager
  for the keyboard.
- `RaiseWindow()` and `LowerWindow()` still take a `TopWindow*`, so they can only
  act on this process's own windows. Acting on a foreign window means going
  through the source, which is what `CycleFocus()` does.
- This package has no timer. It learns of changes from `Paint()`, so a window that
  opens while nothing repaints is noticed at the next repaint rather than within
  500 ms. That is fine for a window that paints its own list and wrong for one
  that has to notice a change without redrawing; `TaskBar` uses a tick instead.
- The headless twin is `upptst/DesktopManagerProbe` (DM2-011). It drives
  `WorkspaceModel` and `WindowListModel` and prints every switch pair, which is
  what the view acts on. It cannot link this package, because this package is a
  MAIN package and its `main` would collide.
- `SetForeground()` does not move the X input focus on a bare X server with no
  window manager, and it did not before the split either. The model's focus flag
  follows U++'s own notion of the foreground window, so the view wiring is
  verifiable while the server-side focus is not. DM2-014 confirmed the model's
  answer is the window that gets raised, on Xvfb both with and without marco.
- `LowerWindow()` is the only method that talks to Xlib directly
  (`XLowerWindow`). It used to be guarded by `flagPOSIX && flagX11`, which
  meant a build without `X11` quietly reduced it to an empty body; it is now
  unconditional and a build without the flag does not compile at all. Nothing
  calls it yet. Note also that `XLowerWindow` only demotes within the sibling
  set; lowering a window below another top-level window needs a
  `XRestackWindows` pass over `Ctrl::GetTopCtrls()`.
- The list order is `_NET_CLIENT_LIST` order when a window manager publishes it,
  which is real stacking order, and `XQueryTree` order otherwise. On a bare Xvfb
  with no window manager, the second applies, so raising a window does not change
  where it sits in the cycle order. This is a property of the fallback, not of the
  model: `WindowListModel::Sync()` keeps a listed window where it is, so the order
  a source reports is the order the user sees.
- Only the current workspace is open. `SetCurrentWorkspace()` closes the old
  window and opens the new one rather than hiding, so switching destroys and
  recreates X windows, losing client state. fluxbox keeps all workspaces alive
  and toggles visibility.
- `RemoveWorkspace()` still removes whichever workspace is current, because that
  is what the model transition is for. Removing an earlier one works too, and
  the current index shifts down with it.
- Workspaces are titled but have no background, icon grid, or window
  placement; `CreateWorkspace()` just opens an empty `TopWindow`. The claimed
  integration with Explorer for the big icon view is not wired up.
- `Paint()` is a diagnostic list, not a themed desktop.
- `DesktopManagerIcons.h` is listed in the manifest and included by
  `DesktopManager.h`, but no icon is actually drawn.
