# Thin Place VR

**See your real wheel, hands and kit inside VR.**

*A thin place is where the veil between two worlds wears thin. This makes a few of them in your cockpit.*

Thin Place VR adds flat "windows" to any OpenXR game, filled with Virtual Desktop's chroma-key colour (magenta). Virtual Desktop replaces that colour with the Quest 3's passthrough cameras, so your real kit shows through exactly where it sits. The wheel window turns with your Simucube, so it hugs the rim at any steering angle.

**Version 0.3.2** — wheel window (Conspit 300GT and 310 APEX), Stream Deck window, hotkey placement.

---

## One-time setup

### 1. Virtual Desktop

On the PC, in the **Virtual Desktop Streamer** app:
- Set the OpenXR runtime to **VDXR** (not SteamVR).

In the headset, open Virtual Desktop's menu → **Streaming** tab:
- Set the frame rate to **90 fps or lower** — the passthrough options can't be ticked above that.
- In the passthrough section, tick **Environment**. Leave **Tracked keyboard**, **Hands** and **Desk portal** off.
- Press **Configure** next to the tickboxes and set the chroma key colour to **Red 255, Green 0, Blue 255** (magenta).
- Start with **Similarity 4%**, **Smoothness 10%**, **Opacity 100%**, so only the windows turn into passthrough.

### 2. Install

1. Download `ThinPlaceVR-Setup-x.x.x.exe` from the **Releases** page of this repo.
   After that, the **Thin Place VR** app's **Check for updates** button keeps it up to date.
2. Run it. Windows will warn about an unknown publisher because the installer isn't signed yet: click **More info** → **Run anyway**.

That's it — the next OpenXR game you start will load it.

---

## The app

Open **Thin Place VR** from the Start menu or desktop. It shows:

- whether the layer is active, which game last used it, and whether your wheel base was found
- your settings — rim, steering range, flip direction, Stream Deck window, passthrough colour, step sizes. Changes apply straight away, even mid-drive.
- **Check for updates**, which downloads and installs the newest version for you
- shortcuts to the log, the check tool and the settings folder

Placing windows is still done in the headset (edit mode, below), because it needs to know where you're looking.

---

## First drive

1. Start **Le Mans Ultimate** or **Assetto Corsa EVO** in VR and get into a car.
2. After a few seconds a window appears straight ahead of you, outlined in yellow, showing your real surroundings. That confirms it's working.
3. Press **Ctrl+Alt+H** to switch on **edit mode**. While it's on, the adjustment keys below go to Thin Place VR instead of the game; every other key works as normal.
4. Fit the rim you're using (the **Conspit 300GT** is selected to begin with; **W** swaps to the **310 APEX**).
5. Hold the wheel straight, sit in your driving position, point your nose at the centre of the wheel and press **P**. The wheel window jumps there (aiming slightly below where your head points, since eyes look lower). Set how far away it lands in the app.
6. Nudge it with the keys below until the real rim fills it.
7. With the wheel dead straight, press **X** to set the centre.
8. Turn the wheel. If the window turns the wrong way, press **I**.
9. Press **Tab** to select the **Stream Deck** window, look at the Stream Deck, press **P**, then nudge and resize it the same way.
10. Press **Esc** (or **Ctrl+Alt+H** again) to leave edit mode and drive.

Everything saves automatically and stays in place next time. Edit mode also switches itself off after 30 seconds without an adjustment key.

---

## Keys

While driving, only two shortcuts are used — everything else goes to the game:

| Key | Does |
|---|---|
| Ctrl+Alt+H | Edit mode on / off |
| Ctrl+Alt+V | All windows on / off |

In **edit mode** (selected window has a yellow outline):

| Key | Does |
|---|---|
| Tab | Select next window (wheel → Stream Deck) |
| P | Place the selected window where you're looking |
| W | Swap wheel: Conspit 300GT ↔ Conspit 310 APEX |
| X | Set steering centre (wheel straight) |
| I | Flip steering direction |
| Arrow keys | Move left / right / up / down |
| R / F | Move away / closer |
| T / G | Tilt top away / towards you |
| Q / E | Turn left / right |
| Z / C | Roll left / right |
| = / − | Selected window bigger / smaller |
| Esc | Leave edit mode |

Hold **Shift** as well for fine steps (1 mm or 0.25°).

---

## Settings and logs

Everything lives in **Documents\Thin Place VR**:

- `config.json` — window positions, wheel choice, steering range (default **900°**, must match Simucube Tuner).

The log is in **C:\ProgramData\Thin Place VR\layer.log** — what happened during the last game session. If something isn't working, this is the file to send.

## Uninstall

Windows **Settings → Apps → Installed apps → Thin Place VR → Uninstall**.

---

## Roadmap

1. **Core windows** — wheel and Stream Deck windows, rotating wheel mask *(this version)*
2. **In-headset editor** — draw and adjust windows with the controller, profiles per game
3. **Hands** — hand-shaped cutouts, gestures as buttons
4. **Sources** — desktop windows, web pages, USB camera, PDFs as panels
5. **Game data** — LMU and AC EVO telemetry panels, Elite Dangerous status panels
6. **Polish** — Stream Deck profile, auto-updates
