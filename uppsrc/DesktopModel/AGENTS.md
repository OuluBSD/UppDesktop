# DesktopModel Package

## Scope
Applies to `uppsrc/DesktopModel` and its entire sub-tree.

## Purpose
- The authoritative, headless state of the X11 desktop packages.
- Three models, one per view that needs state it cannot compute by itself:
  `WorkspaceModel`, `WindowListModel`, `ScreenInfoModel`.
- No presentation, no input, no window system. This package exists so that the
  desktop rules can be exercised with no display and no GUI flag.

This package is a library. It has no `main.cpp`, on purpose: the three views
`uses` it, and a `main` in here would collide with theirs at link time. Its
headless caller is `upptst/DesktopModelCheck`.

## Layout
| File | Role |
|------|------|
| `DesktopModel.upp` | Package manifest; `uses Core` and nothing else |
| `DesktopModel.h` | Main header; includes `Core` and wraps the three sub-headers in `namespace Upp` |
| `WorkspaceModel.h` / `.cpp` | Workspace list, current index, create/remove/switch |
| `WindowListModel.h` / `.cpp` | Ordered window list, title snapshots, focus cycling, per-workspace filter |
| `ScreenInfoModel.h` / `.cpp` | Screen geometry, work area, DPI, read through a `ScreenSource` |
| `AGENTS.md` | This file |

There is no `X11ScreenSource` here. It belongs to the view that needs it, in
`uppsrc/DisplayManager`, because it opens an X connection and this package must
keep building with no GUI flag and no display. See
`ai/info/desktop-model-view-split.md`, section "The X11 screen source".

## The rules these classes obey

The contract is frozen in `ai/info/desktop-model-view-split.md`. In short, a
model here must:

1. Never include `CtrlLib`, and never name `Ctrl`, `TopWindow`, `Draw`,
   `SColorFace`, `Xdisplay` or an Xlib type.
2. Never do I/O, open a connection, or block.
3. Own the authoritative state for its domain. Views own presentation, input
   wiring, and the mapping from a handle to a `TopWindow*`.
4. Return from a transition what the view must do, so that no index is computed
   twice. `SwitchTo()` returns the leaving/arriving pair; `NextFocus()` returns
   the handle.
5. Identify a window by an opaque integer handle, never by a pointer.
6. Work on empty and single-element input, and state its invariants with
   `ASSERT`.
7. Stay silent. No `LOG`. A caller that wants to print asks for `ToString()`.

Rule 1 is enforced by the build rather than by review: `uppsrc/CtrlCore/CtrlCore.h`
carries `#error CtrlCore should not be included without GUI flag`, so a GUI
include in this package cannot compile under the `HEADLESS` mainconfig.

## Model / view split

| Class | Owns | Used by | Replaces |
|-------|------|---------|----------|
| `WorkspaceModel` | workspace count, current index, the three transitions | `DesktopManager` | the `Vector<One<TopWindow>>` and `current_workspace` in `DesktopManager` |
| `WindowListModel` | ordered window entries, title snapshots, focus index and cycling | `DesktopManager`, `TaskBar` | `TaskBar::items` and `DesktopManager::CycleFocus()` |
| `ScreenInfoModel` | per-screen geometry, work area, DPI pair, reserved strips | `DisplayManager` | `screen_geometries`, `work_areas`, `dpi_x`, `dpi_y` and both strip heights |

All three are wired up. The rewiring was DM2-008, DM2-009, DM2-010 and DM2-017;
read the per-method table in each view's `AGENTS.md` before changing a model, since
each caller depends on a specific behaviour of it.

Supporting types, all in `Upp`:

| Type | Role |
|------|------|
| `WorkspaceSwitch` | what a switch did: `changed`, `from`, `to` |
| `WindowEntry` | handle, title snapshot, focus flag, workspace snapshot. Lives in `WindowSource.h` beside the interface since DM5-002, because a source hands these over and the model consumes them |
| `ScreenMetrics` | raw pixel and millimetre metrics as a source reports them |
| `ScreenSource` | the two-method source interface: `GetCount()`, `ReadScreen()` |

## Decisions worth knowing before changing anything

- **The workspace list is a count, not a container.** Workspaces are unnamed,
  unplaced and always opened in list order today, so an empty model with
  `Current() == -1` is a legal state and `CreateWorkspace()` makes the first
  workspace current. When workspaces gain names or placements they belong here,
  not in the view.
- **`RemoveWorkspace()` clamps after the removal, not before.** The original
  `DesktopManager::RemoveWorkspace()` clamped first and then removed, so
  removing the last workspace while it was current left `current_workspace`
  pointing one past the end of the shorter list. That defect is fixed here; do
  not reintroduce the pre-removal clamp.
- **The window order is stable.** `Sync()` keeps a window that is already listed
  where it is, whatever order the source reported this time, and appends newly
  seen handles. This matches what `TaskBar::Sync()` does today, which is what
  makes the rewire in DM2-009 a behaviour-preserving change. The order is now
  inspectable through `HandleAt()` and `ToString()`.
- **Cycling is a query.** `NextFocus()` returns a handle and does not record it
  as focused. The model has no way to know whether a raise succeeded, so
  reporting one would be claiming X state it never read. The view raises the
  window and the next `Sync()` brings the real focus flag back.
- **Both directions are defined when nothing is focused.** Forwards lands on the
  first entry and backwards on the last. The old code defined only the first:
  backwards computed `(-1 - 1 + n) % n` and landed on the second-to-last entry
  for `n > 1`.
- **The workspace filter lives in the model, per DM5-001.** `SetWorkspace()` sets which
  workspace the list answers for; `Sync()` applies it to what the source reported before
  any bookkeeping, a sticky entry (`AllDesktops`) passes every filter, and `AllDesktops`
  as the workspace passes everything. A switch rebuilds the list in the server's order on
  the next sync (so a sticky window sits where the server put it relative to the new
  workspace), while syncs without a switch keep the stable-order rule from 0002. A
  current index larger than any named workspace matches only the sticky window.
- **`WindowEntry` now carries a workspace snapshot.** The DM4-era rule kept it off the
  entry so the model type would not change for a new source; the DM5 contract supersedes
  that for this one field, because a model-side filter needs the workspace of every entry
  at once. `DesktopOf()` stays the live per-handle query.
- **One screen is one record.** `ScreenInfoModel` keeps geometry, work area and
  both DPIs in a single `ScreenRecord` per screen. `DisplayManager` kept four
  parallel vectors, which is what let the work areas run ahead of the
  geometries when a screen count changed.
- **DPI of 0 means "the server reported no physical size".** Virtual machines
  commonly report 0 mm, and the model answers 0 rather than inventing a number.

## Mainconfigs
```
bin/build -m CLANG -M HEADLESS DesktopModel        # library, no GUI flag
```

Both mainconfigs are empty, which is the point: this package must build with no
GUI flag at all. A `GUI` mainconfig here would hide exactly the mistake the
package exists to prevent.

`upptst/DesktopModelCheck` is the headless caller:

```
bin/build -m CLANG -M HEADLESS DesktopModelCheck && ./bin/CLANG/DesktopModelCheck
```

It prints the model state to stdout with `printf` and exits non-zero on the first
broken gate. It runs with no `DISPLAY`, and the binary links no X11, Xft or GTK
library. DM2-013 grew it into the edge-case suite, including `CheckRegressions()`
for the four defects that were found in the GUI packages.

The three per-view headless twins (DM2-011, DM2-012) each drive the same models
from one view's point of view:

```
bin/build -m CLANG -M HEADLESS DesktopManagerProbe && ./bin/CLANG/DesktopManagerProbe
bin/build -m CLANG -M HEADLESS TaskBarProbe        && ./bin/CLANG/TaskBarProbe
bin/build -m CLANG -M HEADLESS DisplayManagerProbe && ./bin/CLANG/DisplayManagerProbe
```

They `uses Core, DesktopModel` and nothing else. They must not add a `uses` line
for a GUI package: those are MAIN packages, and linking one from a check package
collides `main`. A `#include <CtrlLib/CtrlLib.h>` in a twin fails at compile time
with `CtrlCore should not be included without GUI flag`, which is the intended
guard rather than an obstacle.

Every one of these packages carries a `"Valgrind" = "USEMALLOC"` mainconfig
(`WindowSourceCheck` carries `"X11 (Valgrind)" = "USEMALLOC X11"`), added at
DM5-005 so the phase's new paths can be leak-checked the house way:

```
bin/build -m CLANG -M Valgrind DesktopModelCheck
valgrind --leak-check=full --show-leak-kinds=all ./bin/CLANG/DesktopModelCheck
bin/build -m CLANG -M "X11 (Valgrind)" WindowSourceCheck
DISPLAY=:95 valgrind --leak-check=full --show-leak-kinds=all ./bin/CLANG/WindowSourceCheck
```

DM5-005's verdict over the phase's paths: all four (the two probes included)
report `ERROR SUMMARY: 0` and nothing definitely lost; the live run's only
exit blocks are glibc `dlopen` loader bookkeeping, still reachable and
unrelated to this code.

## Known Gaps
- **`GetCount()`, not `ScreenCount()`.** `GetCount()` is the house style:
  `Vector::GetCount()`, `Ctrl::GetCount()` and the rest, so writing `Get` and
  letting Assist complete it lands on the right method instead of an empty list.
  `ScreenCount()` is impossible regardless, because `X11/Xlib.h` defines
  `ScreenCount(dpy)` as a function-like macro and a macro cannot be dodged with
  qualification, which turned `screen.ScreenCount()` into a macro call in the
  first build. All three models use the same name. Do not "fix" it back.
- The window order is still whatever the current source reports, which is
  `Ctrl::GetTopCtrls()` order: per process, and not Z-order. Both views feed it
  in, and the model keeps it because it is the order the source reports. The
  frozen contract for the replacement — a system-wide source reading
  `_NET_CLIENT_LIST` — is in `ai/info/desktop-model-view-split.md`, section "The
  system-wide window source". It is written so that `WindowListModel` itself does
  not change; if a future edit has to touch this class to accommodate a new
  source, the contract was not followed.
- No window placement, EWMH atoms, per-workspace window sets, or theming. Those
  are missing features, and the contract deliberately leaves them out rather
  than guessing where they belong.
