# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

## BSD 3-Clause (third-party portions)

Source files tagged `SPDX-License-Identifier: BSD-3-Clause` include code derived from the third-party works named here and stay under BSD-3-Clause; everything else is MIT.

The exception to "everything else is MIT" is a group of files that carry no tag: `dtsrc/com.cpp`, `dtsrc/com.hpp`, `dtsrc/registry.cpp`, `dtsrc/registry.hpp`, `dtsrc/ISpDataKeyImpl.cpp`, `dtsrc/ISpDataKeyImpl.hpp` and `dtsrc/utils.hpp`. They are Gozaltech's BestSpeech SAPI5 wrapper code with the namespace renamed (see [CREDITS.md](CREDITS.md)), so they are not covered by the MIT License and stay under the terms below.

```
Copyright (c) David Sexton (doubletalk-pc emulator, vendored MAME core)
Copyright (c) Gozaltech (BestSpeech SAPI5 wrapper, from which the COM layer
              of this project is derived)

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Not covered by the MIT License

doubletalkpc.bin, the 512 KB DoubleTalk PC firmware ROM, is proprietary to
RC Systems (https://www.rcsys.com). It is not part of this project and no
license to it is granted by LICENSE or this file. The MIT License and the
BSD-3-Clause terms above cover the SAPI5 wrapper and its documentation only.

The ROM is not present in this source repository. It is bundled inside the
installer published on the Releases page so that the voices work out of the
box. If you are RC Systems and want that bundle removed, please open an issue
or contact the repository owner and it will be taken down.

Anyone building from source rather than using the installer must supply their
own dump; the expected image is CRC32 66685631, SHA-1
bf7e78d6381c76d291ee069971873347a314ffff.
