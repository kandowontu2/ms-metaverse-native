# Third-party notices

## FFmpeg 8.1.2

The native executable statically links a minimal build of FFmpeg 8.1.2. FFmpeg
is licensed under the GNU Lesser General Public License, version 2.1 or later
for this configuration. No GPL or nonfree components are enabled.

The exact configuration, archive hashes, source location, and rebuild details
are recorded in
[`third_party/ffmpeg-static/PROVENANCE.md`](third_party/ffmpeg-static/PROVENANCE.md).
The accompanying LGPL text and FFmpeg license summary are in
[`third_party/ffmpeg-static/licenses`](third_party/ffmpeg-static/licenses).
The exact corresponding FFmpeg source archive is also attached to every binary
GitHub release.

There were no additions, deletions, or modifications to the FFmpeg 8.1.2 source
used for these archives. Application source and build configuration are
provided so recipients can rebuild or relink the executable with a compatible
FFmpeg build.

FFmpeg contains three integer DCT files originating from the Independent JPEG
Group. The Independent JPEG Group is credited here as required by FFmpeg's
license documentation; this project made no changes to those files.

## GCC and MinGW-w64 runtime

The Windows executable is compiled with GCC 13.2.0 for MinGW-w64 and statically
incorporates eligible compiler/runtime support code. GCC runtime components are
covered by the GNU GPLv3 with the
[GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html),
which permits distribution of the compiled program under its own terms when
the exception's conditions are met. MinGW-w64 runtime components retain their
upstream permissive/public-domain terms. No GCC or MinGW runtime DLL is needed.

## Python pefile

The optional PE extraction and public-resource audit tools use
[pefile](https://github.com/erocarrera/pefile), distributed under the MIT
License. It is a development/extraction dependency and is not part of the game
executable.

## Original game content

Original _Ms. Metaverse_ assets are not licensed with this repository or its
public releases. They must be supplied from a lawfully obtained copy by the
user. See [`README.md`](README.md) and [`CREDITS.md`](CREDITS.md).
