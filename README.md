# smooly

**Make any mouse feel premium on Windows.** smooly adds buttery momentum
scrolling, pointer tuning, macOS-style shake-to-find, cursor themes, and
button remapping — in one tiny (~1 MB), native app that
lives in your tray.

<!-- Add a screenshot: docs/screenshot.png -->

---

## Features

### 🖱️ Smooth scrolling
- **Momentum scrolling** — chunky wheel notches become smooth, eased motion.
  Smoothness from *Low* (snappy) to *Buttery* (long glide); short scrolls stay
  responsive (no creeping tail).
- **Speed & acceleration** — set distance per notch; acceleration builds with
  *sustained* fast scrolling, so a quick flick doesn't overshoot.
- **Reverse (natural) direction**, **smooth horizontal (tilt) wheel**, and
  **Shift + wheel → horizontal**.
- **Works in every app** — the smoothing is applied globally.

### 🎯 Pointer
- Set the **OS pointer speed** and **disable "enhance pointer precision"** for
  linear 1:1 movement.
- **Shake to locate** — wiggle the mouse and the cursor smoothly zooms up so you
  can find it, then eases back (the macOS trick).

### 🎨 Cursor
- Bundled **cursor themes** (macOS-like light/dark, Linux-sharp, Amber) in
  **Small / Regular / Large / Extra-Large**, or drop your own `.cur`/`.ani` pack.

### 🔘 Buttons (remapping & gestures)
Register any side button (middle / back / forward) and bind:
- **Click**, **Double-Click**, **Hold** → Middle click, Copy, Paste, Back,
  Forward, **Keyboard shortcut** (press any combo), or **Type text** (types a
  string — your name, an email, a snippet).
- **Click + Scroll** → horizontal scroll, zoom, or fast scroll.
- **Click + Drag** → pan the page, switch virtual desktop, or Task View.

---

## Install

### Winget
```powershell
winget install NischalDahal.smooly
```

### Scoop
```powershell
scoop bucket add extras
scoop install smooly
```

### Installer / portable
Grab the latest from **[Releases](https://github.com/nischal-dahal/smooly/releases)**:
- `smooly-x.y.z-setup.exe` — installer (adds Start-menu entry, optional run-at-startup).
- `smooly-x.y.z-portable.zip` — unzip and run `smooly.exe`, no install.

Releases are **code-signed** (SignPath Foundation), so there's no "unknown
publisher" warning.

---

## Using it

Launch smooly — the settings window opens and it keeps running in the **tray**
(right-click the tray icon for Enabled / Quit). Closing the window just hides it.
Everything applies live and is saved to `%APPDATA%\smooly\config.ini`.

It installs a global mouse hook, so it works in every app. No admin needed except
it can't affect apps running **as administrator** (Windows blocks that).

---

## Build from source

Windows + a C++ toolchain (MinGW-w64 / w64devkit, or MSVC). No external libraries.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

`build.ps1` auto-detects `g++` (PATH, w64devkit, msys2, MinGW) or MSVC and
produces a standalone `smooly.exe`. All tunables live in `config.h`.

- `smooth.cpp` — engine (scroll physics, pointer, cursor, buttons) + the
  custom-drawn dark UI.
- `config.h` — tunables & presets.
- `cursors/`, `fonts/` — bundled assets (ship next to the exe).
- `installer/smooly.iss` — Inno Setup installer.
- `.github/workflows/` — CI + signed release pipeline.
- `packaging/` — Scoop & Winget manifests.

---

## License

MIT — see [`LICENSE`](LICENSE). Bundles the **Inter** font (SIL OFL 1.1) and
**Bibata** cursors (GPL-3.0 / OFL 1.1); see their license files under `fonts/`
and `cursors/`.
