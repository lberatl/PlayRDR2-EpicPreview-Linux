# PlayRDR2-EpicPreview-Linux

A tiny wrapper that lets **Red Dead Redemption 2 (Epic Games Store version)** launch through the **new official Epic Games Launcher Preview for Linux** (tested on 0.120.0, AppImage, GE-Proton 11).

Without it, the Rockstar Games Launcher opens but the game page only shows **"BUY NOW"**, because Epic ownership is not verified.

## Why it is needed

Two things go wrong when the Epic Preview launcher starts RDR2 under Proton:

1. **Rockstar's launcher requires its parent process to be named exactly `EpicGamesLauncher.exe`.** Epic does not provide that process on Linux/Wine. (Credit: this was documented by the Heroic team, see [heroic-epic-integration](https://github.com/BananaWorks07/heroic-epic-integration).)
2. Epic passes `-eglv2apipath` as a **Linux path** (`/home/user/.local/share/com.epicgames.eglv2/api.json`), which is meaningless inside Wine. It needs to be `Z:\home\user\...`.

This project fixes both. It also sets `EOS_LAUNCHED_BY_EPIC=1` (Epic passes `0`); this part is optional, see below.

## How it works

```
Epic Launcher -> PlayRDR2.exe (wrapper) -> EpicGamesLauncher.exe -> PlayRDR2Original.exe -> Rockstar Launcher -> RDR2
```

- `PlayRDR2.exe`: converts the API path, sets the env variable, then starts the helper. After `RDR2.exe` exits it closes the Rockstar Launcher so Epic does not stay stuck on "Running". Writes `wrapper_log.txt` (the auth exchange code is masked).
- `EpicGamesLauncher.exe`: runs its own arguments as a new command and waits. Nothing else.

Both are a few dozen lines of C. Read them: `wrapper.c`, `epic.c`.

## Install

1. Download the files from [Releases](../../releases).
2. In your RDR2 folder, rename the original `PlayRDR2.exe` to `PlayRDR2Original.exe`.
3. Copy `PlayRDR2.exe` and `EpicGamesLauncher.exe` into the RDR2 folder.
4. Close every Rockstar/Wine process first (`pkill -fi rockstar; pkill -f wineserver`), then launch the game from the Epic Launcher.

Note: "Verify/Repair" in Epic will restore the original `PlayRDR2.exe`. Redo the steps after repairing.

## Options (empty marker files in the game folder)

- `wrapper_nopath.txt`: disable the API path conversion.
- `wrapper_noeos.txt`: do not set `EOS_LAUNCHED_BY_EPIC`.
- `wrapper_noclose.txt`: do not close the Rockstar Launcher after the game exits.

## Troubleshooting

Check `wrapper_log.txt`. The `Starting:` line should show `EpicGamesLauncher.exe` and a `Z:\...` API path. If you share the log, remove `-epicusername` and `-epicuserid` first.

You must be logged into a Rockstar/Social Club account that is linked to your Epic account.

## Build

```
x86_64-w64-mingw32-gcc -O2 -o PlayRDR2.exe wrapper.c -luser32
x86_64-w64-mingw32-gcc -O2 -o EpicGamesLauncher.exe epic.c
```

## Status

Works for the author's setup (Arch Linux, KDE, Epic Launcher Preview 0.120.0 AppImage, GE-Proton 11-11). Reports from other setups are welcome via Issues.

## Disclaimer

Unofficial, not affiliated with Epic Games or Rockstar. It does not modify game files, bypass ownership or anti-cheat; it only fixes how the launcher is started. You need to own the game on Epic.

## License

MIT
