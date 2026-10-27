# Third-party notices

mc4_nx is built on the work of other projects. Their licenses apply to their code and to the parts of this
repository derived from it.

## Projects this port is derived from

| Project | Used for | License |
|---|---|---|
| [android32](https://github.com/aks796/android32) by aks796 | `runtime/`, unchanged, at the commit in `runtime/UPSTREAM_COMMIT`: `.so` loader, bionic and Android NDK shims, JNI, EGL, setup, the launcher | MIT (`runtime/LICENSE`); `so_util.c` MIT (Andy Nguyen, fgsfds), `nx32_virtmem.c` ISC (libnx authors) |
| [mc3_nx](https://github.com/Kaays/mc3_nx) | The base of this port: build, setup, audio, video player, launcher, control layout | MIT |
| [mc3-vita](https://github.com/v-atamanenko/mc3-vita) by Volodymyr Atamanenko, with Andy Nguyen and Rinnegatamante | Through mc3_nx: the Gameloft start-up order, DRM flags and key set, followed in `source/mc4_main.c`, `source/mc4_input.c` | MIT |
| [nova3-nx](https://github.com/markaurel13/nova3-nx) by markaurel13 | Reference for a Gameloft game on android32 (no code copied) | see its repository |

## Libraries linked into the NRO

Built by `tools/setup_deps.sh` and the toolchain images; none of their source is in this repository.

| Library | Version | License |
|---|---|---|
| [libnx32](https://github.com/aks796/libnx32) (from vita2hos's AArch32 libnx, from [switchbrew libnx](https://github.com/switchbrew/libnx)) | commit `35c0b5a` | ISC |
| [Mesa](https://mesa3d.org) through [mesa32](https://github.com/aks796/mesa32): EGL, GLES 1/2, glapi, nouveau | 20.1.0-rc3, commit `b02c9c5` | MIT (Mesa's licenses per file) |
| [libdrm_nouveau](https://github.com/devkitPro/libdrm_nouveau) (devkitPro) | 1.0.1 | MIT |
| [FFmpeg](https://ffmpeg.org): libavformat, libavcodec, libavutil | 6.1.2 (`n6.1.2`) | LGPL-2.1-or-later (configured without GPL or non-free parts) |
| miniz (from the vita2hos toolchain image) | as shipped | MIT |
| newlib, libstdc++, libgcc (devkitARM); libnx and newlib (devkitA64, the launcher) | as shipped | newlib's licenses, GCC Runtime Library Exception, ISC |

### FFmpeg (LGPL)

FFmpeg is built unmodified from the `n6.1.2` tag by `tools/build_ffmpeg.sh` and `tools/build_ffmpeg_in.sh`
(the configure line is in the latter) and linked statically. Everything needed to rebuild the NRO with a modified
FFmpeg is in this repository: change or replace the source in `build-ffmpeg/src`, run `tools/build_ffmpeg_in.sh`
through `tools/build_ffmpeg.sh`, then `./build.sh clean && ./build.sh && ./launcher/build.sh`
(see [README.md](README.md#build-from-source)). FFmpeg's source: <https://github.com/FFmpeg/FFmpeg/tree/n6.1.2>.

## Trademarks

Modern Combat and Gameloft are trademarks of Gameloft. Nintendo Switch is a trademark of Nintendo. Xperia is a
trademark of Sony. The launcher icon (`launcher/icon.jpg`) and the README banner (`extras/banner.jpg`) are the game's
artwork, © Gameloft, used to identify the game. This project is not affiliated with or endorsed by any of them.
