# Hologram

A lightweight Windows desktop widget that displays an animated GIF overlay on your screen and prevents your computer from going idle.

## Features

- **Animated GIF overlay** — transparent, borderless, always-on-top window displaying an animated GIF
- **Mouse jiggle** — automatically moves the mouse slightly every 2 minutes to prevent screen lock / sleep
- **Draggable** — left-click and drag to reposition the widget anywhere on screen
- **Start with Windows** — right-click context menu option to add/remove from Windows startup (Registry)
- **Position memory** — saves and restores window position between sessions
- **Hidden from taskbar** — runs without cluttering your taskbar

## Requirements

- Windows OS
- Go 1.21+

## Build

```bash
go build -ldflags "-H windowsgui" -o hologram-go.exe .
```

The `-H windowsgui` flag hides the console window.

## Run

```bash
.\hologram-go.exe
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

## Original Project

This is a Go port of the original C# WinForms hologram application.
