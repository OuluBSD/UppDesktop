# Core/Desktop/Free Package

## Scope
Applies to `uppsrc/Core/Desktop/Free` and its files.

## Purpose
- FreeDesktop.org specification implementations (XDG Base Directory, Icon Theme,
  MIME Applications, Desktop Entry, Trash).
- Concrete `IDesktopBackend` implementation for Linux/BSD and other POSIX
  desktops that follow the FreeDesktop specs.
- All logic is GUI-free and HEADLESS-buildable; only Core types are used.

## Layout

| File | Role |
|------|------|
| `Free.upp` | Manifest; `uses Core, Core/Desktop`; HEADLESS mainconfig |
| `DesktopFree.h` | Umbrella header wrapping the five modules |
| `DesktopFree.cpp` | `FreeDesktopBackend` implementing `IDesktopBackend`; registers at static init |
| `XdgDirs.h/.cpp` | XDG Base Directory + `user-dirs.dirs` parsing |
| `XdgIconTheme.h/.cpp` | Icon Theme Specification (index.theme, inheritance, fallback) |
| `XdgMime.h/.cpp` | MIME Applications (globs, globs2, aliases, subclasses, magic) |
| `XdgDesktopEntry.h/.cpp` | Desktop Entry Spec (.desktop parsing, mimeapps.list) |
| `XdgTrash.h/.cpp` | Trash Specification (files/info, .trashinfo, cross-fs) |
| `AGENTS.md` | This file |

## Key Types (re-exported from modules)
- `XdgRoots`, `XdgUserDir`, `LoadUserDirs`, `ResolveUserDir`
- `IconTheme`, `IconLookupConfig`, `IconLookup`, `LoadIconTheme`, `LookupIcon`
- `MimeGlob`, `MimeGlob2`, `MimeAlias`, `MimeSubclass`, `MimeMagic`, `MimeInfo`, `MimeLookupConfig`, `MimeDetectResult`, `LoadMimeDatabase`, `DetectMime`
- `DesktopEntry`, `DesktopEntryConfig`, `LoadDesktopEntry`, `LoadDesktopEntries`, `FindDesktopEntry`, `GetDefaultApp`, `ShouldShowEntry`, `ExpandExec`
- `TrashDir`, `TrashConfig`, `TrashEntry`, `TrashResult`, `FindTrashDirs`, `ListTrash`, `MoveToTrash`, `RestoreFromTrash`, `DeleteFromTrash`, `EmptyTrash`, `FindTrashEntry`

## Backend Contract
`FreeDesktopBackend` implements `IDesktopBackend`:
- **XDG dirs**: `GetUserDirectory` → `ResolveShellFolder` (the legacy
  single-file chain Core/App wrapped), with `ResolveUserDir`/`LoadUserDirs`
  as the spec-shaped helpers for new callers
- **MIME**: `DetectMime` / `GetMimeDescription` → `DetectMime` + `GetMimeInfo`
- **Icons**: `LookupIcon` / `LookupAppIcon` → `LookupIcon` (delegates to XdgIconTheme)
- **Apps**: `GetDesktopApp` / `GetDefaultApps` → `FindDesktopEntry` + `GetDefaultApp`
- **Trash**: `GetTrashInfo` / `MoveToTrash` / `RestoreTrash` / `EmptyTrash` → Trash module

## Configuration Injection
All modules accept explicit config structs (`XdgRoots`, `IconLookupConfig`, etc.) so
fixture tests can supply synthetic roots without touching the host environment.
Production code uses `FreeDesktopConfig::FromEnv()` to build configs from the
real environment.

## Build
```
bin/build -m CLANG -M HEADLESS Core/Desktop/Free
```

## Dependencies
- `Core`
- `Core/Desktop` (for `IDesktopBackend` and result types)

## Known Gaps
- No `mime/*.xml` parsing for descriptions/generic icons (uses cache only).
- Magic matching is simplified (no full regex, limited mask support).
- Desktop Entry localized keys use `LC_MESSAGES` only; no full locale fallback chain.
- Trash cross-volume detection uses `stat.st_dev` (POSIX only).