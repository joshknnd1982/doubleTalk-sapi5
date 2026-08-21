# Credits

This project stands on two others. Neither is incidental — one supplies the
engine that actually speaks, the other supplied the SAPI5 architecture this
wrapper was built from.

## doubletalk-pc — the emulator

<https://github.com/daiverd/doubletalk-pc>

Copyright © David Sexton. BSD-3-Clause.

Everything that produces sound here is that project's work. `dtalk.dll` and
`dtalk64.dll` are its standalone emulator builds: a vendored MAME 80C188EB CPU
core plus the board simulation (I/O, DAC, output stage) that runs the
DoubleTalk PC's original firmware. The reverse-engineering behind it — working
out the card's audio path, its index-marker timing, and the pitch-preserving
speech-rate table rescale used for the rate-boost setting — is all David
Sexton's.

The C API this wrapper drives (`dtalk_create`, `dtalk_synth16`,
`dtalk_read_index_marks`, `dtalk_set_lowpass_hz`, and the rest) is that
project's `dtalk.h`, unchanged. The NVDA synthesizer driver in that repository
was also the reference for how the card's command set should be sequenced —
particularly that `nO` must be emitted first because it reloads the entire
per-voice parameter block, and that number mode `14B` is needed so a screen
reader does not lose the leading zeros in "007".

## BestSpeech SAPI5 wrapper — the SAPI5 architecture

<https://github.com/gozaltech/bstspeech-sapi> — by Gozaltech
(<http://gozaltech.org>)

This project began as that wrapper and reuses its COM plumbing directly: the
`IUnknown`/`IClassFactory` template layer, the object and interface reference
counting, the registry helpers, `ISpDataKeyImpl`, and — most valuable of all —
the approach of serving voices from an in-memory `IEnumSpObjectTokens`
registered through `Speech\Voices\TokenEnums`, so individual voices need no
registry entries at all. Those files here are that project's code with the
namespace renamed.

If you find this wrapper useful, the BestSpeech authors accept donations at
<https://paypal.me/gozaltech>.

### What is different here

Recorded so the lineage is clear, not to claim the design:

- No 32-bit helper process and no named-pipe bridge. BestSpeech needed them
  because its engine DLL was x86-only; the DoubleTalk emulator ships for both
  architectures, so each SAPI DLL drives it in-process.
- Word-boundary and bookmark events are timed from the firmware's own index
  markers rather than estimated from text offsets.
- Tunable settings live in an INI file instead of the registry.
- The installer registers the 64-bit engine through the 64-bit registry view.
  The BestSpeech installer passed its x64 DLL to the 32-bit `regsvr32`, which
  leaves the voices invisible to 64-bit applications.

## RC Systems — the firmware

<https://www.rcsys.com>

The DoubleTalk PC hardware and its 512 KB firmware ROM are RC Systems' work.
**The ROM is proprietary to RC Systems and is not covered by this project's
licence.** It is not in this source repository; it is bundled inside the
installer on the Releases page so the voices work without a separate download.
If you are RC Systems and want that removed, open an issue and it will be taken
down. Anyone building from source supplies their own dump — see the README for
where to put it and the checksums to verify it against.

The eight voice names — Perfect Paul, Vader, Big Bob, Precise Pete, Ricochet,
Biff, Skip and Robo Robert — and the command set are RC Systems'.

## MAME

The 80C188EB CPU core inside the emulator comes from
[MAME](https://www.mamedev.org/), vendored by the doubletalk-pc project under
BSD-3-Clause.
