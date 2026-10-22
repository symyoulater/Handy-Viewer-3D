# Handy Viewer 3D

See your real wheel, hands and Stream Deck while driving in VR.

Handy Viewer 3D adds flat "windows" to any OpenXR game, filled with Virtual Desktop's chroma-key colour (magenta). Virtual Desktop replaces that colour with the Quest 3's passthrough cameras, so your real kit shows through exactly where it sits. The wheel window turns with your Simucube, so it hugs the rim at any steering angle.

**Version 0.1** — wheel window (Conspit 300GT and 310 APEX), Stream Deck window, hotkey placement.

---

## One-time setup

### 1. Virtual Desktop

On the PC, in the **Virtual Desktop Streamer** app:
- Set the OpenXR runtime to **VDXR** (not SteamVR).

In the headset, open Virtual Desktop's menu → **Streaming** tab:
- Turn on **VR Passthrough** with chroma keying.
- Set the key colour to **R 255, G 0, B 255** (magenta).
- Keep similarity and smoothness low, so only the windows turn into passthrough.

### 2. Install

1. Download `HandyViewer3D-Setup-x.x.x.exe` from the **Releases** page of this repo.
2. Run it. Windows will warn about an unknown publisher because the installer isn't signed yet: click **More info** → **Run anyway**.

That's it — the next OpenXR game you start will load it.

---

## First drive

1. Start **Le Mans Ultimate** or **Assetto Corsa EVO** in VR and get into a car.
2. Fit the rim you're using (the **Conspit 300GT** is selected to begin with; **Ctrl+Shift+F4** swaps to the **310 APEX**).
3. Hold the wheel straight, sit in your driving position, look at the centre of the wheel and press **Ctrl+Shift+F3**. The wheel window jumps to where you're looking, 60 cm in front of you, outlined in yellow.
4. Nudge it with **Ctrl+Shift + numpad** until the real rim fills it (see the table below).
5. With the wheel dead straight, press **Ctrl+Shift+F5** to set the centre.
6. Turn the wheel. If the window turns the wrong way, press **Ctrl+Shift+F6**.
7. Press **Ctrl+Shift+F2** to select the **Stream Deck** window, look at the Stream Deck, press **Ctrl+Shift+F3**, then nudge and resize it the same way.

Everything saves automatically and stays in place next time.

---

## Hotkeys

All hotkeys work while the game has focus. Hold **Ctrl+Shift**, then:

| Key | Does |
|---|---|
| F1 | All windows on / off |
| F2 | Select next window (wheel → Stream Deck) |
| F3 | Place the selected window where you're looking |
| F4 | Swap wheel: Conspit 300GT ↔ Conspit 310 APEX |
| F5 | Set steering centre (wheel straight) |
| F6 | Flip steering direction |
| Numpad 4 / 6 | Move left / right |
| Numpad 8 / 2 | Move up / down |
| Numpad 9 / 3 | Move away / closer |
| Numpad 7 / 1 | Tilt top away / towards you |
| Numpad / and * | Turn left / right |
| Page Up / Page Down | Roll left / right |
| Numpad + / − | Stream Deck window bigger / smaller |

Hold **Alt** as well for fine steps (1 mm or 0.25°).

The selected window shows a yellow outline for a few seconds after each change.

---

## Settings and logs

Everything lives in `%LOCALAPPDATA%\HandyViewer3D\`:

- `config.json` — window positions, wheel choice, steering range (default **900°**, must match Simucube Tuner).
- `layer.log` — what happened during the last game session. If something isn't working, this is the file to send.

## Uninstall

Windows **Settings → Apps → Installed apps → Handy Viewer 3D → Uninstall**.

---

## Roadmap

1. **Core windows** — wheel and Stream Deck windows, rotating wheel mask *(this version)*
2. **In-headset editor** — draw and adjust windows with the controller, profiles per game
3. **Hands** — hand-shaped cutouts, gestures as buttons
4. **Sources** — desktop windows, web pages, USB camera, PDFs as panels
5. **Game data** — LMU and AC EVO telemetry panels, Elite Dangerous status panels
6. **Polish** — Stream Deck profile, auto-updates
