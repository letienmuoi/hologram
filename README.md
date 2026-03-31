# Hologram

A lightweight Windows desktop widget that displays an animated GIF overlay on your screen and prevents your computer from going idle.

## Features

- **Animated GIF overlay** — transparent, borderless, always-on-top window displaying an animated GIF
- **Per-pixel alpha transparency** — smooth edges with no color-key fringe artifacts
- **Anti-aliased downscaling** — area-average (box filter) for crisp, artifact-free scaling
- **Mouse jiggle** — automatically moves the mouse slightly every 2 minutes to prevent screen lock / sleep
- **Draggable** — left-click and drag to reposition the widget anywhere on screen
- **Start with Windows** — right-click context menu option to add/remove from Windows startup (Registry)
- **Position memory** — saves and restores window position between sessions
- **Hidden from taskbar** — runs without cluttering your taskbar

## Implementations

### Go

**Requirements:** Windows, Go 1.21+

```bash
go build -ldflags "-H windowsgui" -o hologram-go.exe .
```

### C (Win32 / MinGW)

**Requirements:** Windows, MSYS2 MinGW-w64, CMake, Ninja (or MinGW Make)

```bash
cd c
mkdir build && cd build
cmake -G "Ninja" ..
ninja
```

Or with MinGW Makefiles:

```bash
cd c
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

Output: `c/build/hologram.exe` — single portable executable, no external DLLs needed.

## Run

```bash
.\hologram-go.exe    # Go version
.\c\build\hologram.exe  # C version
```

## Usage

| Action | Description |
|---|---|
| Left-click + drag | Move the widget |
| Right-click | Open context menu |
| Context menu → Start with Windows | Toggle auto-start on login |
| Context menu → Exit | Close the application |

## Customization

Replace `hologram/Resources/yy3.gif` with your own GIF image and rebuild.

## Project Structure

```
main.go              # Go implementation
c/
  main.c             # C/Win32 implementation (built-in GIF decoder)
  CMakeLists.txt     # CMake build config
  resource.rc        # Embeds GIF into executable
hologram/
  Resources/
    yy3.gif          # Animated GIF asset
```

## Original Project

This is a rewrite of the original C# WinForms hologram application, available in both Go and C.
