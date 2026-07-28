# ps1-cdloader

A minimal, bare-metal PlayStation 1 **CD loader**: it unlocks the CD drive and
boots a burned / imported disc. It builds to a standard **PS-EXE** you can run
from a modchipped console, an already-unlocked console, or an emulator.

This is a stripped-down, cleaned-up derivative of
[tonyhax International](https://github.com/alex-free/tonyhax). It keeps the
core loader logic (drive unlock + disc boot) plus Game-ID broadcasting, and
drops the anti-anti-piracy database, GameShark cheat engine and exploit entry
points. It is built with a plain freestanding
MIPS toolchain in the spirit of
[ps1-bare-metal](https://github.com/spicyjpeg/ps1-bare-metal) — no SDK.

## What it does

1. Reinitialises the kernel and brings up a small on-screen text console.
2. Detects the CD controller version and drive region.
3. On unlockable, non-Japanese drives, runs the no$-documented "secret unlock"
   command sequence (`50h`..`56h` with the "Licensed by / Sony / Computer /
   Entertainment / <region>" strings) so the drive will read copied / imported
   discs. (On a modchip or emulator the drive is usually already unlocked; this
   step is then harmless.)
4. Reads the disc that is **already in the drive** - no lid swap needed, since an
   unlocked drive can `ReadN` an unlicensed disc directly. If that read fails
   (e.g. you booted from a separate loader disc, or the drive spun down), it
   falls back once to the classic "open and close the lid" swap prompt.
5. Reads `SYSTEM.CNF` from the disc (falling back to `PSX.EXE`).
6. If a compatible device is present in memory-card slot 1, sends the game's
   canonical Game-ID before resetting the bus/kernel state.
7. Loads the referenced executable and jumps to it.

## Game-ID support

The loader supports the established PS1 Game-ID command used by:

- 8BitMods MemCard Pro and MemCard Pro 2
- SD2PSX
- PicoMemcard-based devices and other compatible firmware
- Devices that monitor the same controller/memory-card command stream

For ordinary discs, the `BOOT` entry is reduced to a stable ID such as:

```text
SYSTEM.CNF: BOOT = cdrom:\GAMES\SLUS_005.94;1
Game-ID:    cdrom:SLUS_005.94;1
Wire data:  81 21 00 14 63 64 72 6f 6d 3a ... 3b 31 00
```

The first three bytes select the memory-card bus and start the Game-ID command;
the length byte includes the trailing null byte.

Early Japanese games often identify their executable only as `PSX.EXE`, which
is not unique. For those discs the loader reads the ISO-9660 Primary Volume
Descriptor at sector 16 and uses the volume creation timestamp to recover the
SLPS/SCPS serial from Tonyhax International's maintained lookup table. If a
timestamp is unknown, booting remains safe and the loader falls back to the
filename ID.

Detection and transmission failures are non-fatal. A normal Sony memory card,
an empty slot, or an unsupported device will time out quickly and the game
continues to boot. After every attempt the loader reinitializes the BIOS/CD and
card-controller state before loading the game executable.

The low-level transport comes from the official
[Cybdyn Systems MemCardPro-ASM](https://github.com/Cybdyn-Systems/MemCardPro-ASM)
library. Its Apache-2.0 license and attribution are in
`third_party/memcardpro/`.

By default the loader boots whatever disc is already inserted (fast, no lid
step). If you are booting from a dedicated *loader disc* and always need to swap
to a different game disc, build with `-DWAIT_FOR_SWAP=1` added to `CFLAGS` to
force the lid-swap prompt before every boot.

## How it boots discs (honest note)

Booting an arbitrary retail disc reliably means reusing the PlayStation kernel:
the ISO9660 file access (`FileOpen`/`FileRead`), `CdReadSector`, `SetConf` and
the `DoExecute` hand-off are all BIOS calls, exactly as tonyhax and the stock
shell do. Reimplementing ISO9660 + the executable loader from scratch would be
large and fragile for no practical gain. Everything else — the build, the GPU
text console, the CD-controller unlock and the orchestration — is plain
bare-metal C with direct register access.

The loader links itself high in RAM (`0x801EA300`, matching tonyhax) so it stays
resident while a
game loads at the usual `0x80010000`. If a game is large enough to overlap the
loader, it falls back to the BIOS `LoadAndExecute`.

## Building

Requires a little-endian MIPS toolchain and Python 3. On Ubuntu:

```sh
sudo apt-get install gcc-mipsel-linux-gnu python3
make
```

This produces `cdloader.exe`. To use a different toolchain prefix (for example
the `mipsel-none-elf-` GCC that ps1-bare-metal uses):

```sh
make CROSS=mipsel-none-elf-
```

A `CMakeLists.txt` and toolchain file are also provided for parity with
ps1-bare-metal-style projects:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mipsel.cmake
cmake --build build
```

GitHub Actions (`.github/workflows/build.yml`) builds `cdloader.exe` on every
push and uploads the PS-EXE and linker map as a downloadable artifact.

## Running

- **Emulator (DuckStation, PCSX-Redux, no$psx, …):** mount your game image, then
  run `cdloader.exe` directly (Open EXE / drag-and-drop) - it unlocks and boots
  the mounted disc.
- **Real console (cheat cart / unirom + serial, modchip, or already-unlocked):**
  insert your backup / import CD-R, then launch `cdloader.exe` however you run
  homebrew - it unlocks the drive and boots the inserted disc, no lid step.

To force the classic lid-swap prompt (for booting from a dedicated loader disc),
build with `-DWAIT_FOR_SWAP=1` added to `CFLAGS`.

## Limitations

- **PS1 only.** PS2 quirks are not a target; some PS2-in-PS1-mode behaviour
  (e.g. video-mode switching) is intentionally not handled.
- **Japanese consoles**: the licence backdoor does not apply, so support is
  best-effort (a basic `SetSession` path is included, but the swap-trick flow
  of full tonyhax is not reproduced).
- This is a loader, not a copy-protection defeat for games that check the disc
  themselves; tonyhax's per-game anti-anti-piracy database was removed.

## Credits & licence

Derived from **tonyhax** / **tonyhax International** by socram8888, alex-free
and contributors. Licensed under the **GNU GPL v3** (see `LICENSE`). If you
redistribute, keep the attribution and licence intact.

Game-ID protocol references and imported components:

- [PS1 Disc-Based Game ID](https://github.com/jdfr228/PS1-Disc-Based-Game-ID)
- [Tonyhax International Game-ID implementation](https://github.com/alex-free/tonyhax/blob/master/gameid.md)
- [Cybdyn Systems MemCardPro-ASM](https://github.com/Cybdyn-Systems/MemCardPro-ASM)

Third-party license texts and source notes are retained under `third_party/`.
