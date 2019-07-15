# VU Meter Plugin for the DeaDBeef audio player

## How to build
1. clone the repo or download as zip
2. extract the zip and change to the directory
3. issue `make`
4. it should have created a gtk2 and a gtk3 folder with the plugin .so files in it.

## Installation
You can run the `userinstall.sh` script or simply copy the gtk2/gtk3 .so file to
`${HOME}/.local/lib/deadbeef`, create the folder if it does not exist.
If you want to use the retro VU meter also copy the `vumeterStereo.png` to the same directory



## Screenshots


### VU Bars
![vu meter in bars mode](https://github.com/macearl/ddb_vu_meter/raw/master/screenshots/VU.png "VU meter in bars mode")

### retro VU meter
![vu meter in retro mode](https://github.com/macearl/ddb_vu_meter/raw/master/screenshots/retroVU.png "VU meter in retro mode")
