# Desktop Services Migration and Capability Matrix

This note is a plan deliverable of the desktop-services extraction. It maps the
old, duplicated desktop helpers to the new `Core/Desktop` / `Draw/Desktop`
entry points and records the exact package facts a later `Ctrl/*` integration
needs. It is documentation only: the compatibility wiring it describes is
applied in stages, and nothing here changes a public signature by itself.

## Capability matrix

Every backend implements the same `IDesktopBackend` contract. A capability the
platform stub does not implement returns an explicit `DESKTOP_UNSUPPORTED`
status; it never silently falls back to a Linux path.

| Capability | Free | Win | Mac | Amiga | Android |
|------------|------|-----|-----|-------|---------|
| Backend identity + diagnostics | yes | yes (name) | yes (name) | yes (name) | yes (name) |
| XDG base dirs + user directories | yes | no | no | no | no |
| MIME detection (globs/globs2) | yes | no | no | no | no |
| MIME aliases / subclasses | yes | no | no | no | no |
| MIME content sniffing (magic) | yes (simplified) | no | no | no | no |
| MIME descriptions | cache only | no | no | no | no |
| Icon theme lookup (identity + path) | yes | no | no | no | no |
| Desktop entry parse + metadata | yes | no | no | no | no |
| Default application (mimeapps.list) | yes | no | no | no | no |
| OpenWith candidate list | not yet | no | no | no | no |
| Trash capability + move/restore/empty | yes | no | no | no | no |

"no" means the method returns `DESKTOP_UNSUPPORTED` with a detail string. The
Win/Mac/Amiga/Android packages are compile-safe stubs; their intended native
implementations are recorded in each package `AGENTS.md`.

Draw-side materialization (`Image` from an icon result) is common; the platform
Draw stubs currently delegate to the common raster loader.

## Old symbol to new entry point

| Old location | Old symbol | New entry point |
|--------------|------------|-----------------|
| `Core/App.cpp` | `GetPathXdg` | `XdgRoots::FromEnv` + `LoadUserDirs` |
| `Core/App.cpp` | `GetPathDataXdg` | `ResolveUserDir`; the `GetShellFolder` chain uses `ResolveShellFolder` |
| `Core/App.cpp` | `GetShellFolder`, `GetDesktopFolder`, `GetDocumentsFolder`, `GetDownloadFolder`, `GetMusicFolder`, `GetPicturesFolder`, `GetVideoFolder` | **wired**: delegate to `IDesktopBackend::GetUserDirectory` under `Core.upp uses Core/Desktop`, with the hand-rolled reader kept as an in-function fallback |
| `Core/App.cpp` | `GetDesktopManager` | `IDesktopBackend::Diagnostics` (reports `XDG_CURRENT_DESKTOP`) |
| `CtrlLib/FileSel.cpp` | `ExtToMime` (GTK globs) | **wired**: deleted; `FileExtMaker` asks `IDesktopBackend::DetectMime` (`CtrlLib.upp` lists `uses Core/Desktop/Free` on POSIX so the backend registers) |
| `CtrlLib/FileSel.cpp` | GTK theme icon lookup | **stays native (GtkThemeIcon)**: theme choice comes from XSETTINGS and theme icons render SVG, which the backend materializer cannot do without an SVG raster plugin; `FileExtMaker` feeds it the backend's hyphenated MIME names exactly as ExtToMime did |
| `Explorer/ExplorerModel.cpp` | `GetMimeFromExtension` | **wired**: delegate to `IDesktopBackend::DetectMime` when a backend answers, with the hand table kept verbatim as the fallback (`GetMimeFromExtension` -> `HardcodedMimeFromExtension`); icon kinds bucket by prefix, so the better MIME values land in the same kind |
| `Explorer/ExplorerModel.cpp` | `~/.config/user-dirs.dirs` parsing and place labels | **wired**: `LoadUserDirs(roots, check_existence=false)` supplies the declared directories; the model keeps the mount-manager existence check, the home-row skip, the dedupe and the labels - `LoadUserDirs`' own raw host check must not answer for a VFS mount |
| `Explorer/ExplorerModel.cpp` | fixed `~/.local/share/Trash/files` discovery | **wired**: `IDesktopBackend::GetTrashInfo` names the trash, the mount check stays in the model; `can_move` no longer creates the trash directories on a query |
| `Explorer/ExplorerIcons.cpp` | drawn MIME icons | stays as the final fallback; provider entry point is the icon identity above |

## Wiring constraints discovered

1. **`Core/App` delegation is wired, with the old reader kept as fallback.**
   The earlier cycle fear was wrong in detail: umk builds `Core/Desktop`
   before `Core` and does not reject the reverse `uses`. What matters is that
   `Core/Desktop` alone selects no backend, so `Core` cannot *link* a platform
   backend without losing platform neutrality. The adopted shape:
   `Core.upp` lists `uses Core/Desktop`, `Core/App.cpp` includes
   `<Core/Desktop/Desktop.h>`, and its POSIX `GetShellFolder` hands the two
   keys to `IDesktopBackend::GetUserDirectory` when a supported backend is
   registered in the process. Binaries that link no desktop backend keep the
   original hand-rolled reader as an in-function fallback, so a pure Core
   build keeps its previous answers (and `Core` stays free of any platform
   package). Byte parity of the backend chain with the old reader -
   single-file selection over `user-dirs.dirs`, `user-dirs.defaults`,
   config-dirs `user-dirs.dirs` plus last-wins keys, asymmetric quote
   stripping, and `$ENV` expansion - is checked by the shell-folder chain
   battery in `upptst/DesktopServicesCheck`.
2. `CtrlLib` and `Explorer` can depend on `Core/Desktop` without a cycle
   (`CtrlLib -> Core -> Core/Desktop`, `Explorer -> Core -> Core/Desktop`).
3. FreeDesktop backends must be registered before the first query. The
   integration package should list `uses Core/Desktop/Free` (or the platform
   backend) and *call the actuator* (`RegisterFreeDesktopBackend()`), since
   static-archive linking only pulls a package's object in when one of its
   symbols is referenced; a static-init self-registration alone never
   activates. `CtrlLib/FileSel` registers and, in the GTK branch, also reads
   the GTK `gtk-icon-theme-name` setting into `SetPreferredIconTheme`, so
   backend icon lookups see the theme the desktop is actually showing.
4. Backends must not leak their registration. A backend registered with
   `new` and no owner trips the debug heap diagnostic's exit check
   ("Heap leaks detected!"); register a static-lifetime singleton, as both
   the Free backend and the platform stubs now do.
5. `upptst/DeskIconProbe` fingerprints the CtrlLib icon answers (pixel hashes
   per case). Run it before and after any further icon-service change:
   `bin/umk "uppsrc,upptst" DeskIconProbe CLANG +GUI,SHARED` and diff the two
   outputs; the wiring above produced a byte-identical run.

## Later `Ctrl/*` package rename (recorded, not performed)

| Current | Later name |
|---------|------------|
| `CtrlCore` | `Ctrl/Core` |
| `CtrlLib` | `Ctrl/Lib` |
| Explorer UI modules | `Ctrl/Explorer` |

A consumer of this work should record, at integration time:

- the exact `uses(...)` additions (`Core/Desktop`, `Core/Desktop/Free`,
  `Draw/Desktop`),
- the mainconfig/flag changes (`flagGUI` stays off for headless builds; the
  backend stubs build with the empty and `HEADLESS` mainconfigs),
- the include path (`<Core/Desktop/Desktop.h>`, `<Core/Desktop/Free/DesktopFree.h>`,
  `<Draw/Desktop/DesktopDraw.h>`).

## Verification status

- `Core/Desktop` and every Core platform subpackage build headless with no
  compile errors.
- `Draw/Desktop` and every Draw platform subpackage build without `flagGUI`
  with no compile errors.
- `CtrlLib` builds GUI with the FileSel wiring; no compile errors across the
  Desktop family and CtrlLib (`+GUI,SHARED`; the only diagnostics are umk's
  forced library link, which fails on this sandbox's missing static `libz`).
- `upptst/DesktopServicesCheck` runs 63 fixture checks with no failures (XDG
  precedence, MIME weights/magic/aliases/subclasses, icon lookup, desktop-entry
  association, default apps, trash metadata, round trip and same-basename
  collisions (numbered trash names, separate .trashinfo records), plus the
  shell-folder chain parity battery and the host-backend actuator check).
- `Core/App`'s XDG user-directory wrappers delegate to the registered backend
  and keep the in-function fallback; the delegation is exercised under the
  same binary that runs the parity battery, so a default-headless test run
  covers the live path, not only the fallback.
- FileSel icon answers verified pixel-identical before/after the wiring via
  `upptst/DeskIconProbe` on a battery of extensions, special names and
  paths; the backend answers (`detect x.png -> image/png`) feed the same
  GTK-theme icon names the deleted ExtToMime produced.
- Explorer wired and verified through its own headless twin:
  `bin/umk uppsrc Explorer CLANG +HEADLESS,SHARED`, then
  `Explorer --places --view list /tmp/kilo/explorer-fixture` before and after
  the wiring - the places, listing and column output are identical (only the
  machine's live free-space numbers differ), the small-icons run shows the
  same kind buckets, and the GUI configuration builds and links without
  errors (`bin/umk uppsrc Explorer CLANG +GUI,SHARED`).
- GUI binaries link in the sandbox with `+GUI,SHARED` (e.g.
  `bin/umk upptst Clipboard CLANG +GUI,SHARED`); the plain CLANG method still
  fails at link because it demands static `libz` and no static libraries are
  installed, which is an environment property, not a package problem.
- Disabled: no source under `Core/Desktop`, `Draw/Desktop`, or the check package
  references `tmp/xdg-specs`.
