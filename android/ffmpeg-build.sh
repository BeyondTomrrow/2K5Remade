set -e
export TMPDIR=/e/claude-overflow/fftmp TMP=/e/claude-overflow/fftmp TEMP=/e/claude-overflow/fftmp
NDK=C:/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64
OUT=C:/Android/src/ffmpeg-android
rm -rf /c/Android/src/ffmpeg-obj; mkdir -p /c/Android/src/ffmpeg-obj && cd /c/Android/src/ffmpeg-obj
../ffmpeg-7.1.1/configure --prefix=$OUT \
  --target-os=android --arch=aarch64 --cpu=armv8-a --enable-cross-compile \
  --cc=$NDK/bin/clang.exe --host-cc=$NDK/bin/clang.exe --host-cflags="--target=aarch64-linux-android28 --sysroot=$NDK/sysroot" --host-ld=$NDK/bin/clang.exe --cxx=$NDK/bin/clang++.exe --ld=$NDK/bin/clang.exe \
  --ar=$NDK/bin/llvm-ar.exe --nm=$NDK/bin/llvm-nm.exe --ranlib=$NDK/bin/llvm-ranlib.exe --strip=$NDK/bin/llvm-strip.exe \
  --sysroot=$NDK/sysroot --extra-cflags="--target=aarch64-linux-android28 -O2 -fPIC" --extra-ldflags="--target=aarch64-linux-android28" \
  --enable-static --disable-shared --enable-pic --disable-programs --disable-asm --disable-doc --disable-debug \
  --disable-everything --disable-avdevice --disable-avfilter --disable-postproc --disable-swscale --enable-swresample \
  --disable-network --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-vulkan --disable-mediacodec --disable-jni \
  --enable-decoder=mp3,mp3float,aac,aac_latm,flac,vorbis,opus,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8,wmav1,wmav2 \
  --enable-encoder=wmav2 \
  --enable-demuxer=mp3,aac,mov,flac,ogg,wav,asf,matroska \
  --enable-muxer=asf --enable-parser=mpegaudio,aac,flac,vorbis,opus --enable-protocol=file
for f in Makefile ffbuild/config.mak; do sed -i 's#/c/Android/#C:/Android/#g' $f; done
MAKE=/c/Android/sdk/ndk/27.2.12479018/prebuilt/windows-x86_64/bin/make.exe
$MAKE -j12
$MAKE install || true
cp ../ffmpeg-7.1.1/libavutil/*.h $OUT/include/libavutil/ 2>/dev/null || cp /c/Android/src/ffmpeg-7.1.1/libavutil/*.h /c/Android/src/ffmpeg-android/include/libavutil/
cp libavutil/avconfig.h libavutil/ffversion.h /c/Android/src/ffmpeg-android/include/libavutil/
