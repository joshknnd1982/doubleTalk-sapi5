# DoubleTalk PC SAPI5

A native Windows SAPI5 voice engine for the **RC Systems DoubleTalk PC**, an
early-1990s ISA text-to-speech card, running under MAME component emulation.
No SAPI4, no DOS, no hardware — the card's original firmware runs on an emulated
80C188EB CPU and its audio is handed straight to SAPI5.

Works with any SAPI5 application: NVDA, JAWS, Narrator, Balabolka, Bookworm, and
anything else that speaks through Windows.

## What you get

- **32-bit and 64-bit engines**, each registered in its own registry view, so
  both 32-bit and 64-bit applications see the voices.
- **All eight documented card voices** — Perfect Paul, Vader, Big Bob, Precise
  Pete, Ricochet, Biff, Skip, Robo Robert.
- **Every card parameter exposed**: rate, pitch, volume, articulation,
  expression, formant frequency, reverb and tone, plus the host-side
  reconstruction filter and a pitch-preserving rate boost.
- **A configuration utility** built for keyboard and screen-reader use, where
  every change is applied, spoken and saved immediately.
- **Real word-boundary and bookmark events**, timed from the firmware's own
  index markers rather than estimated from text offsets — so highlighting
  follows the audio exactly.

## Two things worth knowing about the hardware

**There are five distinct voices, not eight.** The manual documents eight names,
but this firmware takes the `nO` voice number modulo 5: Biff, Skip and Robo
Robert render byte-for-byte identically to Perfect Paul, Vader and Big Bob at
their defaults. All eight are still offered — they are the card's documented
names, and they diverge as soon as you move any per-voice parameter. Verified by
rendering the same sentence on a freshly created emulator instance per voice and
comparing the PCM.

**The card is English-only.** There is no language-select command anywhere in
the DoubleTalk command set — all 26 command letters were probed against the ROM
and cross-checked against RC Systems' own command summary. The "English or
Spanish" claim in RC's literature refers to the later RC8660 chip, and even
there Spanish requires a user-supplied downloadable exception dictionary; this
card's firmware does not implement the download commands at all.

## Parameter ranges

These are the firmware's real ranges, established by sweeping each command
against the ROM — not the later RC8660 datasheet's, which differ.

| Command | Parameter | Range |
|---|---|---|
| `nO` | Voice | 0–9, wraps mod 5 |
| `nS` | Rate | 0–9 |
| `nP` | Pitch | 0–99 |
| `nV` | Volume | 0–9 |
| `nA` | Articulation | 0–9 |
| `nE` | Expression | 0–9 |
| `nF` | Formant frequency | 0–9 |
| `nR` | Reverb | 0–9 |
| `nX` | Tone | 0–2 (bass / normal / treble) |

Every value above wraps modulo its range, so `15A` is `5A`.

## Settings and the registry

Tunable state lives in an INI file, not the registry:

- `%LOCALAPPDATA%\DoubleTalkSAPI\settings.ini` — your settings
- `%PROGRAMDATA%\DoubleTalkSAPI\settings.ini` — machine defaults, used only
  when a user has no file of their own

The engine re-reads the file whenever it changes, so an adjustment in the
configuration utility is audible on the very next thing spoken — no restart.

The **only** registry keys this project writes are the three SAPI needs in order
to find the voices at all:

```
HKLM\Software\Classes\CLSID\{0B5F5547-...}\InProcServer32
HKLM\Software\Classes\CLSID\{C54B5B3F-...}\InProcServer32
HKLM\Software\Microsoft\Speech\Voices\TokenEnums\DoubleTalk
```

The eight voices themselves have no registry presence — they are served from
memory by the token enumerator. If HKLM is not writable the engine registers
under HKCU instead, though note that SAPI only *discovers* voice enumerators
from HKLM, so a per-user registration is useful for testing rather than for
everyday use.

## Logging

Both the engine and the utility log to `%LOCALAPPDATA%\DoubleTalkSAPI\logs\`,
one file per component, architecture and process, capped at 4 MB with one
rotation. The level is set in the configuration utility (`LogLevel` 0–4) and
defaults to Normal. The installer always writes its own log; find it via
`%TEMP%\Setup Log*.txt`.

## Building

```batch
build_all.bat
```

**Requirements:** Windows 10+, Visual Studio 2022 Build Tools (C++ workload),
CMake 3.15+, and — for the installer — Inno Setup 6.

The build produces both architectures, runs the engine self-test on each, and
compiles the installer.

## Tools

`dt_render.exe` renders text to a WAV through the same code path the SAPI engine
uses, which is the first place to look when deciding whether a problem is in the
engine or in the SAPI plumbing:

```batch
dt_render --self-test
dt_render --list
dt_render --voice 3 --reverb 7 --out pete.wav "Hello there"
dt_render --all-voices --outdir samples
```

`dt_sapitest.exe` drives the real SAPI stack — `SpVoice`, format negotiation,
the event queue — while creating the voice token from our own enumerator, so the
engine can be exercised without an elevated install:

```batch
dt_sapitest --list
dt_sapitest --events "One two three"
dt_sapitest --all --outdir sapi_samples
```

Note that SAPI only queues events to the voice when speaking to a real audio
device; with `--out` the events go to the file's own stream instead, so
`--events` reports none.

## Credits

Two projects made this possible, and [CREDITS.md](CREDITS.md) sets out exactly
what came from where.

**[doubletalk-pc](https://github.com/daiverd/doubletalk-pc)** by David Sexton —
BSD-3-Clause. Everything that actually produces sound is that project's work:
`dtalk.dll` and `dtalk64.dll` are its standalone emulator builds, a vendored
MAME 80C188EB core plus the board simulation that runs the card's firmware. The
C API this wrapper drives is its `dtalk.h`, and its NVDA driver was the
reference for how the card's command set must be sequenced.

**[BestSpeech SAPI5 wrapper](https://github.com/gozaltech/bstspeech-sapi)** by
Gozaltech — this project began as that wrapper. Its COM plumbing is reused
directly: the `IUnknown`/`IClassFactory` template layer, the reference
counting, the registry helpers, and the in-memory `IEnumSpObjectTokens`
approach that keeps individual voices out of the registry entirely. If this is
useful to you, they accept donations at <https://paypal.me/gozaltech>.

**RC Systems** (<https://www.rcsys.com>) made the DoubleTalk PC hardware, its
firmware, its command set, and the eight voice names.

## Licensing and the firmware ROM

The code written for this project is licensed under the MIT License — see
[LICENSE](LICENSE). The third-party portions keep their own terms, and their
notices are in [NOTICE.md](NOTICE.md).

`doubletalkpc.bin`, the 512 KB firmware ROM, is **proprietary to RC Systems and
is not covered by that licence.** It is not in this repository. It is bundled
inside the installer on the Releases page so the voices work out of the box; if
you are RC Systems and want that removed, open an issue and it will be taken
down.

Building from source instead? Supply your own dump and put it next to the built
binaries (or in the install directory). The expected image is CRC32 `66685631`,
SHA-1 `bf7e78d6381c76d291ee069971873347a314ffff` — the engine checks the size at
load and logs a clear error if it is missing or wrong.
