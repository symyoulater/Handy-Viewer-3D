# Handy Viewer 3D

See your real wheel, hands and Stream Deck while driving in VR.

Handy Viewer 3D adds flat "windows" to any OpenXR game, filled with Virtual Desktop's chroma-key colour (magenta). Virtual Desktop replaces that colour with the Quest 3's passthrough cameras, so your real kit shows through exactly where it sits. The wheel window turns with your Simucube, so it hugs the rim at any steering angle.

**Version 0.1.1** — wheel window (Conspit 300GT and 310 APEX), Stream Deck window, hotkey placement.

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
2. After a few seconds a window appears straight ahead of you, outlined in yellow, showing your real surroundings. That confirms it's working.
3. Fit the rim you're using (the **Conspit 300GT** is selected to begin with; **Ctrl+Alt+4** swaps to the **310 APEX**).
4. Hold the wheel straight, sit in your driving position, look at the centre of the wheel and press **Ctrl+Alt+3**. The wheel window jumps to where you're looking, 60 cm in front of you.
5. Nudge it with the keys below until the real rim fills it.
6. With the wheel dead straight, press **Ctrl+Alt+5** to set the centre.
7. Turn the wheel. If the window turns the wrong way, press **Ctrl+Alt+6**.
8. Press **Ctrl+Alt+2** to select the **Stream Deck** window, look at the Stream Deck, press **Ctrl+Alt+3**, then nudge and resize it the same way.

Everything saves automatically and stays in place next time.

---

## Hotkeys

All hotkeys work while the game has focus, on any keyboard (no numpad or F-keys needed). Hold **Ctrl+Alt**, then:

| Key | Does |
|---|---|
| 1 | All windows on / off |
| 2 | Select next window (wheel → Stream Deck) |
| 3 | Place the selected window where you're looking |
| 4 | Swap wheel: Conspit 300GT ↔ Conspit 310 APEX |
| 5 | Set steering centre (wheel straight) |
| 6 | Flip steering direction |
| A / D | Move left / right |
| W / S | Move up / down |
| R / F | Move away / closer |
| T / G | Tilt top away / towards you |
| Q / E | Turn left / right |
| Z / C | Roll left / right |
| = / − | Stream Deck window bigger / smaller |

Hold **Shift** as well for fine steps (1 mm or 0.25°).

The selected window shows a yellow outline for a few seconds after each change.

---

## Settings and logs

Everything lives in **Documents\Handy Viewer 3D**:

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
