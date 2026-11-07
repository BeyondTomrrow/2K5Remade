#!/bin/sh
# Build SDL3 on Ubuntu 22.04: X11, Wayland, ALSA, PulseAudio, PipeWire, udev are
# all dlopen'd, so the library runs without any of them present.
set -e
cmake -S /root/deps/SDL -B /root/deps/sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/root/sdl3 -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DSDL_TEST_LIBRARY=OFF -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_X11_SHARED=ON -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
  -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_PIPEWIRE=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_VULKAN=ON -DSDL_GPU=ON \
  -DSDL_LIBUDEV=ON -DSDL_LIBUDEV_SHARED=ON -DSDL_DBUS=ON -DSDL_IBUS=ON -DSDL_KMSDRM=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON
cmake --build /root/deps/sdl-build -j6
cmake --install /root/deps/sdl-build
ls -la /root/sdl3/lib
echo SDL-BUILD-DONE
