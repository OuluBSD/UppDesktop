# Core/Desktop/Win Package

## Scope
Applies to `uppsrc/Core/Desktop/Win`.

## Purpose
- Windows implementation of `IDesktopBackend` (stub).
- Registers `WinDesktopBackend` when linked on `PLATFORM_WIN32`.

## Layout
| File | Role |
|------|------|
| `Win.upp` | Manifest; `uses Core, Core/Desktop`; HEADLESS mainconfig |
| `DesktopWin.h` | Forward declaration for registration |
| `DesktopWin.cpp` | `WinDesktopBackend` stub; `RegisterWinDesktopBackend()` |
| `AGENTS.md` | This file |

## Current State
All methods return `DESKTOP_UNSUPPORTED`. Full implementation would use:
- SHGetKnownFolderPath for user directories
- Registry for MIME associations
- Shell32 for icon extraction
- Shell link resolution for .lnk files
- IShellFolder/Recycle Bin for trash

## Build
```
bin/build -m CLANG -M HEADLESS Core/Desktop/Win
```