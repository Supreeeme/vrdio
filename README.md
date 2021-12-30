# VRdio - Audio Controls in VR for Linux
Control volume, audio device configurations, and set a default audio configuration all from the SteamVR dashboard.
(a screenshot would be great to have here but screenshots unfortunately don't work in steamvr linux valve pls fix)

## Dependencies
- SteamVR
- Pulseaudio (or Pulseaudio stand in, such as pipewire-pulse) + libpulse
- Vulkan ICD Loader (`libvulkan1` on Ubuntu, `vulkan-icd-loader` on Arch)
- SDL (`libsdl2-2.0-0` on Ubuntu, `sdl2` on Arch)
- libopengl-dev (Ubuntu) / libglvnd (Arch)
- Qt5/Qt6 (probably already installed - if not, look at [Qt's dependencies](https://doc.qt.io/qt-5/linux-requirements.html))

## How do I get it?
Grab the latest release, place and extract it wherever you like, and double click (or run from terminal) `vrdio-launch.sh` while SteamVR is open. It will be autolaunched next time you run SteamVR.
If you want to uninstall it from SteamVR, run `./vrdio-launch.sh --uninstall`.

## Features to come
- [ ] Audio mirroring

Feel free to request features!

## Building
Dependencies:
- libpulse
- libvulkan
- Qt6 libraries (qt6-base, qt6-declarative)
```
mkdir build
cd build
cmake ..
make
```
This will place the `vrdio` binary in the top level directory. Libraries used for building releases are bundled in the latest release.
