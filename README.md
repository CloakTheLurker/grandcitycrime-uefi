# Grand City Crime

A GTA-style game set in the real Miami that boots straight from UEFI, no Windows under it. The whole game is `game.efi`, 964,608 bytes, and the CPU draws every pixel.

Made by: https://www.youtube.com/@CodingWalaXD

## run

```
sh build.sh
mkdir -p esp/EFI/BOOT && cp game.efi esp/EFI/BOOT/BOOTX64.EFI
qemu-system-x86_64 -machine q35 -m 512 -bios OVMF.fd -drive format=raw,file=fat:rw:esp
```

Build needs clang and lld-link, or skip it and use the `game.efi` that's already here. Only ran in qemu so far.

Arrows or WASD to drive, Enter to get in and out, mouse for the camera, M for the map, J in a taxi for fares, 1 2 3 for weapons, Ctrl to shoot.

The map is baked with `osm/bake.py` from OpenStreetMap, Miami-Dade County data, a NOAA lidar scan and NAIP photos. `lidar/steps.txt` shows how, credits are in `MAP_DATA.txt` and `PEOPLE_DATA.txt`.
