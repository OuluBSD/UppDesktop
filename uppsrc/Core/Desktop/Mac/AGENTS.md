# Core/Desktop/Mac Package

## Scope
Applies to `uppsrc/Core/Desktop/Mac`.

## Purpose
- macOS implementation of `IDesktopBackend` (stub).
- Registers `MacDesktopBackend` when linked on `PLATFORM_MACOS`.

## Layout
| File | Role |
|------|------|
| `Mac.upp` | Manifest; `uses Core, Core/Desktop`; HEADLESS mainconfig |
| `DesktopMac.h` | Forward declaration for registration |
| `DesktopMac.cpp` | `MacDesktopBackend` stub; `RegisterMacDesktopBackend()` |
| `AGENTS.md` | This file |

## Current State
All methods return `DESKTOP_UNSUPPORTED`. Full implementation would use:
- `NSFileManager` / `NSHomeDirectory` for user directories
- `UTType` / Launch Services for MIME/UTI
- `NSImage` / `NSWorkspace` for icons
- `LSGetApplicationForInfo` for default apps
- `NSFileManager` + `~/.Trash` for trash

## Build
```
bin/build -m CLANG -M HEADLESS Core/Desktop/Mac
```