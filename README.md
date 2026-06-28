# Doom for PRIME

Port of [doomgeneric](https://github.com/ozkl/doomgeneric) to **PRIME standalone devices** (Prime 4, PRIME GO, etc.). Runs directly on the framebuffer with full MIDI DJ controller support and ALSA audio.

![Demo](https://img.shields.io/badge/status-stable-green)

## Requirements

- A **PRIME device** (Prime 4, PRIME GO, etc.) with SSH access and root shell
- Follow [icedream/denon-prime4](https://github.com/icedream/denon-prime4) instructions to enable SSH and gain root access
- A USB Ethernet adapter or network connection to the device
- A **Doom 1.9 IWAD** file (`doom.wad`) — obtain from your legally purchased copy of Doom

## Quick Start

### 1. Build

Using Docker:

```sh
docker build -t doomgo-builder .
docker run --rm doomgo-builder cat /doomprimego > doomprimego
```

Or download the pre-built binary from the [Releases](https://github.com/your-username/DoomGo/releases) page.

### 2. Copy to device

```sh
scp doomprimego root@192.168.52.235:/data/
```

Also copy a `doom.wad` (Doom 1.9 IWAD) to `/data/`:

```sh
scp doom.wad root@192.168.52.235:/data/
```

### 3. Set up the service on the device

SSH into the PRIME GO and run:

```sh
# Create launcher script
cat > /data/doomgo.sh << 'DOOMSH'
#!/bin/sh
systemctl stop engine.service 2>/dev/null
sleep 1
killall -9 engine Engine 2>/dev/null
chvt 1 2>/dev/null
dd if=/dev/zero of=/dev/fb0 bs=1024 count=1024 2>/dev/null
cd /data
./doomprimego -iwad doom.wad
systemctl start engine.service 2>/dev/null
DOOMSH
chmod +x /data/doomgo.sh

# Replace the SoundSwitch service
cat > /etc/systemd/system/soundswitch.service << 'SERVICE'
[Unit]
Description=Doom on PRIME GO

[Service]
Type=simple
ExecStart=/data/doomgo.sh
ExecStopPost=/bin/sh -c 'systemctl start engine.service 2>/dev/null'
TimeoutStopSec=5
Restart=no
LimitCORE=infinity

[Install]
WantedBy=multi-user.target
SERVICE

systemctl daemon-reload
systemctl enable soundswitch.service
systemctl restart engine.service
```

### 4. Alternative: use PrimeLauncher

[**PrimeLauncher**](https://github.com/erhan-/primelauncher) provides a nice on-screen menu to launch Doom and other apps without having to tap the SoundSwitch icon. Install it alongside DoomGo for a seamless launcher experience.

### 5. Play

Tap the **SoundSwitch** (lighting) icon in Engine DJ's main toolbar (or use PrimeLauncher). Engine OS will stop and Doom will appear. Exit Doom to return to Engine DJ.

## MIDI Controls

| PRIME GO Button | MIDI | Doom Action |
|----------------|------|-------------|
| **PLAY** (left deck) | Ch2 Note 10 | Fire / Shoot |
| **CUE** (left deck) | Ch2 Note 9 | Use / Open |
| **SYNC** (left deck) | Ch2 Note 8 | Run |
| **HOT CUE 1–4** | Ch2 Notes 15–18 | Weapons 1–4 |
| **FWD** (browse) | Ch15 Note 4 | Walk forward |
| **BACK** (browse) | Ch15 Note 3 | Walk backward |
| **VIEW** (browse) | Ch15 Note 7 | Menu / Escape |
| **LOAD** (browse) | Ch15 Note 6 | Enter / Select |
| **Jog wheel** (left) | Ch2 CC77 | Strafe left/right |

## Display

The PRIME GO screen is 800×1280 (portrait). Doom renders at 640×400, rotated 90° counter-clockwise and scaled 2× to fill the display. Prime 4 uses a different resolution — adjust `DOOMGENERIC_RESX`/`DOOMGENERIC_RESY` and the rendering code accordingly.

## Audio

8-bit unsigned PCM at 11025Hz, software-mixed across 8 channels, output via ALSA `plughw:1,0`.

## Building Locally

Requires `crossbuild-essential-armhf` and `libasound2-dev:armhf`. Build with:

```sh
arm-linux-gnueabihf-gcc -O2 -flto -DFEATURE_SOUND \
    -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 \
    -I. -o doomprimego \
    doomgeneric_primego.c \
    $(find doomgeneric/doomgeneric -name "*.c" ! -name doomgeneric_primego.c) \
    -lm -lasound
```

## Credits

- **ghuntley** — pioneering Denon DJ Linux research and documentation
- **icedream** — [denon-prime4](https://github.com/icedream/denon-prime4) project providing essential SSH/root access instructions — this is a **prerequisite** for installing DoomGo
- **ozkl** — [doomgeneric](https://github.com/ozkl/doomgeneric) portable Doom framework that made this port possible
- **erhan-** — [PrimeLauncher](https://github.com/erhan-/primelauncher) on-screen launcher menu for PRIME GO
- **id Software** — the original Doom
- **Denon DJ** — for the PRIME GO hardware

## Disclaimer

This project is **not affiliated with, endorsed by, or sponsored by** id Software, Bethesda Softworks, or inMusic Brands. "Doom" is a registered trademark of id Software LLC. All trademarks and registered trademarks are property of their respective owners.

This project is provided for educational and personal use only. You must own a legitimate copy of Doom to obtain the required IWAD file.

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE).
DoomGo is a derivative work of [doomgeneric](https://github.com/ozkl/doomgeneric) and inherits its GPL-2.0 license.
