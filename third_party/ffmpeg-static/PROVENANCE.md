# FFmpeg static build provenance

- Upstream: FFmpeg
- Version: 8.1.2
- Source: <https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz>
- Source SHA-256: `464BEB5E7BF0C311E68B45AE2F04E9CC2AF88851ABB4082231742A74D97B524C`
- Release-signing fingerprint: `FCF986EA15E6E293A5644F10B4322F04D67658D8`
- License for this configuration: LGPL 2.1 or later
- Source modifications: none
- Target: x86_64 MinGW-w64 / Windows

Exact configuration:

```text
--prefix=/c/codex-build/ms-metaverse-ffmpeg-8.1.2/install --target-os=mingw32 --arch=x86_64 --cc=gcc --cxx=g++ --ar=ar --ranlib=ranlib --strip=strip --pkg-config=pkg-config --disable-shared --enable-static --disable-programs --disable-doc --disable-debug --disable-network --disable-autodetect --disable-everything --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample --enable-decoder=cinepak,msvideo1,msrle,pcm_u8,pcm_s16le --enable-demuxer=avi,wav --enable-protocol=file --enable-small
```

The build does not enable `--enable-gpl`, `--enable-version3`, or
`--enable-nonfree` and does not link external codec libraries.

Bundled archive SHA-256 hashes:

```text
B6FAD97D6BA8401D05574D49B4190955403DE11B81F33DB517D95B867320FC29  libavcodec.a
01D85BB25157DB00F7D647E4F03204B59860BCC17C0FDE81677CD5535B8E4AA5  libavformat.a
AC2578F09FC996BFD7118ABC878158F8A73FB4E5A3A57F487352BF5F5584774C  libavutil.a
065AB64E95B92BFA8BCDCC18844153491BF14D1ABB6C2FB4D499167A5C712716  libswresample.a
7FD8C40B4196AB571F9098F6763AC594E14DEAF1F0ACDF6466070B8C80A140DB  libswscale.a
```

To reproduce the libraries, extract the upstream source in an MSYS2 MinGW64
shell, run the configuration above with a writable local `--prefix`, then run:

```sh
make -j$(nproc)
make install
```

Copy the five installed static archives and required installed headers into
this directory. The application can then be rebuilt and relinked normally with
CMake. Every GitHub release carrying the statically linked executable also
carries the exact upstream `ffmpeg-8.1.2.tar.xz` corresponding source archive
and its upstream detached signature. Use
`scripts/prepare-ffmpeg-release-source.ps1` to download, hash-check, and verify
those files from FFmpeg's server.
