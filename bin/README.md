# bin — the emulated card

Two of the three files the engine needs live here. The third you supply.

## Shipped here

| File | What it is |
|---|---|
| `dtalk.dll` | 32-bit DoubleTalk PC emulator |
| `dtalk64.dll` | 64-bit DoubleTalk PC emulator |

Both are standalone builds from
[doubletalk-pc](https://github.com/daiverd/doubletalk-pc) by David Sexton,
BSD-3-Clause — a vendored MAME 80C188EB CPU core plus the board simulation
(I/O, DAC, output stage) that runs the card's original firmware. They are
included prebuilt so this repository can be built without also building the
emulator; see that project to build them yourself (`make win32` / `make win64`).

## Not shipped here: `doubletalkpc.bin`

The 512 KB firmware ROM is **proprietary to RC Systems** and is deliberately
excluded from this repository — `.gitignore` has an entry for it, which is
there on purpose and should not be removed to make a build succeed.

If you installed from the Releases page you already have it; the installer
places it in the install directory.

Building from source instead? Drop your own dump in this folder. Verify it
first:

```powershell
certutil -hashfile doubletalkpc.bin SHA1
```

| Property | Expected |
|---|---|
| Size | 524,288 bytes |
| CRC32 | `66685631` |
| SHA-1 | `bf7e78d6381c76d291ee069971873347a314ffff` |

The engine checks the size when it loads and writes a clear error to the log if
the file is missing or the wrong length, so a bad ROM shows up as a logged
failure rather than as silence.

## Where these files end up

`CMakeLists.txt` stages all three next to the built binaries, so everything runs
straight from the build tree. At install time the emulator DLLs and the ROM go
in the install root; the 32-bit SAPI engine lives in `x86\` and finds them by
looking one directory up.
