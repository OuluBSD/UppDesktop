# Core/Desktop/Amiga Package

## Scope
Applies to `uppsrc/Core/Desktop/Amiga`.

## Purpose
- AmigaOS implementation of `IDesktopBackend` (stub).
- Registers `AmigaDesktopBackend` when linked on `PLATFORM_AMIGA`.

## Layout
| File | Role |
|------|------|
| `Amiga.upp` | Manifest; `uses Core, Core/Desktop`; HEADLESS mainconfig |
| `DesktopAmiga.h` | Forward declaration for registration |
| `DesktopAmiga.cpp` | `AmigaDesktopBackend` stub; `RegisterAmigaDesktopBackend()` |
| `AGENTS.md` | This file |

## Current State
All methods return `DESKTOP_UNSUPPORTED`. Full implementation would use:
- AmigaDOS `GetVar()` / `ENV:` for directories
- `MimeTypes` / `DefIcons` for MIME/icons
- Workbench `.info` files for app metadata
- `Trashcan` handler for trash

## Build
```
bin/build -m CLANG -M HEADLESS Core/Desktop/Amiga
```