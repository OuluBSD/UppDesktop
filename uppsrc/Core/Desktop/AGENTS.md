# Core/Desktop Package

## Scope
Applies to `uppsrc/Core/Desktop` and its entire sub-tree of U++ packages.

## Purpose
- The GUI-free desktop service contract (`IDesktopBackend`) and the backend
  selection entry point.
- Backend implementations live in the platform subpackages
  (`Free`, `Win`, `Mac`, `Amiga`, `Android`); this package holds only the
  contract, the result types and the registration/selection machinery.

## Why this package exists and not a file in Core

`Core/App.cpp` already contains desktop-service behavior (XDG user directories,
desktop-session identification, browser launching) and `CtrlLib/FileSel.cpp`
contains a private GTK MIME/theme implementation. Both are duplicates of what
a backend should answer once, and both are reachable only from callers that
already know the platform. This package separates the *contract* from the
*platform*, so:

- a headless caller can ask about MIME, icons, applications and trash without
  testing `PLATFORM_*` or including any GUI header;
- the FreeDesktop implementation can be built and fixture-tested in isolation;
- the platform stubs can be swapped without touching the callers.

## Layout

| File | Role |
|------|------|
| `Desktop.upp` | Manifest; `uses Core`, empty mainconfigs so the package builds with no GUI flag |
| `Desktop.h` | Umbrella header; includes the contract and result types inside `namespace Upp` |
| `DesktopCommon.h` | Result/status types (no namespace, included from inside `namespace Upp`) |
| `DesktopBackend.h` | `IDesktopBackend` interface |
| `Desktop.cpp` | Backend registration and selection |
| `MIGRATION.md` | Old symbol → new entry point map, capability matrix, wiring constraints, later `Ctrl/*` rename notes |
| `AGENTS.md` | This file |

## The contract

`IDesktopBackend` is stateless and GUI-free. It never returns `Image`; icon
answers are identity and resolution metadata, and materialization belongs in
`Draw/Desktop`. Every method that can fail returns a `DesktopStatus` and a
detail string, so "no icon theme installed" (OK, empty file path) is
distinguishable from "icon lookup is unsupported here" (UNSUPPORTED).

Backends register with `RegisterDesktopBackend()`; `GetDesktopBackend()`
returns the first registered one; a caller that wants to substitute a backend
for testing calls `RegisterDesktopBackend()` before the first query.

**The registration needs an actuator, not static-init alone.** A static
archive member is only linked when one of its symbols is referenced from
elsewhere, so a backend that only registers at static-init never activates in
an app that links nothing else of the package. Each platform package exports
an actuator (`RegisterFreeDesktopBackend()` and the per-platform ones) and the
consumer calls it once before the first lookup - `CtrlLib/FileSel` does this
in its icon path. Register a static-lifetime singleton, not `new`: an owning
registry would otherwise trip the debug heap diagnostic's exit check. The
free-time contract also carries icon-theme preference: a GUI layer that knows
the active theme (GTK's `gtk-icon-theme-name`, KDE's `Icons/Theme`) publishes
it through `SetPreferredIconTheme` so backend lookups answer with the theme
the user is actually seeing.

## Dependencies

- `Core` only. This package must not depend on `Draw`, `CtrlCore`, `CtrlLib`,
  or any platform GUI header. `uppsrc/CtrlCore/CtrlCore.h` carries
  `#error CtrlCore should not be included without GUI flag`, which is the
  build-enforced guard for that rule.

## Known Gaps

- Backend registration order across translation units is not guaranteed, so
  only one platform backend should be linked into any given binary. The
  conflict is reported through `GetDesktopBackendCount()` rather than hidden.
- No process-global cache of the active icon theme or default applications;
  backends read fresh on each call, which is what makes a theme change visible
  to a headless client.