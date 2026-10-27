#!/bin/sh
# build_ffmpeg_in.sh -- runs inside the toolchain container (build_ffmpeg.sh).
set -eu
NX=$DEVKITPRO/libnx32
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec"
PREFIX=/work/portlibs32
B=/work/build-ffmpeg/obj
rm -rf "$B" # a fresh configure: objects of an earlier attempt must not mix in
mkdir -p "$B" "$PREFIX"
cd "$B"

# -fno-short-enums: FFmpeg's enums must be int-sized (devkitARM's default is
# short enums; FFmpeg passes AVERROR codes through enum-typed returns, and
# GCC 14 rejects its enum* / int* mixes). The port builds mc4_video_ff.c the
# same way; nothing else of ours sees FFmpeg's structs.
#
# configure links small test programs: the same specs as the port (libnx's
# crt0 and newlib), so each probe sees what the real link will.
/work/build-ffmpeg/src/configure \
  --prefix="$PREFIX" \
  --enable-cross-compile --cross-prefix=arm-none-eabi- \
  --arch=arm --target-os=none \
  --extra-cflags="$ARCH -fno-short-enums -O2 -ffunction-sections -fdata-sections -D__SWITCH__ -isystem $NX/include" \
  --extra-ldflags="$ARCH -Wl,-z,notext -specs=$NX/switch32.specs -L$NX/lib" \
  --extra-libs="-lnx -lm" \
  --enable-static --disable-shared \
  --disable-autodetect --disable-everything \
  --disable-programs --disable-doc --disable-debug \
  --disable-network --disable-avdevice --disable-swscale --disable-swresample \
  --disable-postproc --disable-avfilter \
  --disable-asm --disable-runtime-cpudetect --disable-pthreads \
  --enable-protocol=file \
  --enable-demuxer=mov \
  --enable-decoder=h264,mpeg4,aac \
  --enable-parser=h264,mpeg4video,aac

make -j"$(nproc)"
make install
echo "build_ffmpeg_in.sh: installed into portlibs32/ ($(ls "$PREFIX"/lib/libav*.a | tr '\n' ' '))"
