# Core/Desktop/Android Package

## Scope
Applies to `uppsrc/Core/Desktop/Android`.

## Purpose
- Android implementation of `IDesktopBackend` (stub).
- Registers `AndroidDesktopBackend` when linked on `PLATFORM_ANDROID`.

## Layout
| File | Role |
|------|------|
| `Android.upp` | Manifest; `uses Core, Core/Desktop`; HEADLESS mainconfig |
| `DesktopAndroid.h` | Forward declaration for registration |
| `DesktopAndroid.cpp` | `AndroidDesktopBackend` stub; `RegisterAndroidDesktopBackend()` |
| `AGENTS.md` | This file |

## Current State
All methods return `DESKTOP_UNSUPPORTED`. Full implementation would use:
- `Context.getFilesDir()` / `Environment.getExternalStorageDirectory()` for dirs
- `MimeTypeMap` / `Intent` for MIME
- `PackageManager` / `Drawable` for icons
- `ResolveInfo` / `Intent` for apps
- No standard trash; would use `MediaStore` or app-specific cache

## Build
```
bin/build -m CLANG -M HEADLESS Core/Desktop/Android
```