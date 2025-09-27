# Picostation

Original Fork from [paulocode](https://github.com/paulocode/picostation)
Forked from [megavolt85](https://github.com/megavolt85/PicoStation)
Special thanks to [ManiacVera](https://github.com/ManiacVera) for the support and trying to help me understand why the uf2 wasn't working at all for me, i also merged his fix for games with broken boot into this firmware.

I'm taking some experimental changes and fixes by manually reviewing the code and with the help of AI or automated tools since i don't have much knowledge on the Playstation system, testing every build with incompatible, different region and precedently reported not booting games.
I'll try to keep the master branch clean and only release quality checked firmware, after managing to implement and fix a few key points i'll focus on taking changes on the UI design, once done i'll just upstream changes or accept pull request with fixes.
Since i have noticed other repos doesn't have clear instructions and don't track issues, feel free to open issues and pull request here, i'll update the read me with simpler instructions to build and flash a working picoStation aswell as build firmware from this repo (which is being already pretty much automatized, i'll add a setup.sh to auto install deps)

## __In developement__ _Raspberry Pi Pico based ODE_ for the original Playstation
<a href="https://twitter.com/paulo7x8/status/1602007862733312000"><img src="https://i.ibb.co/9hT2GQc/pico-tweet.jpg" alt="original tweet" height="400"/></a>

### Supported models:
- PU-8  (SCPH100X)
- PU-18 (SCPH55XX)

### Compatibility
<b>NOTE: rename your cue-sheet to UNIROM.cue</b><br>
- Game compatibility and reliability is greatly improved from the original Picostation repo, but there will still be games that don't work at all, and some that may freeze randomly or run poorly.
- ~~Some games may load (see <a href="https://github.com/paulocode/picostation/wiki/Game-Compatibility-List">Game Compatibility List</a> wiki page)~~

### How-to
- see <a href="https://github.com/paulocode/picostation/wiki/How-to">How-to</a> wiki page

### Notes
- Please make sure your SD card is formatted as exFAT.
- Firmware updates can be installed by copying a new `PICOSTATION.UF2` file to the root of the SD card. The Pico will stage and
  apply the update automatically on the next boot before launching the menu. Make sure the UF2 file is 1MB or smaller so it fits
  in the reserved update space.


### To-do
- ~~Stabilize image loading~~
- Make an interface for image choice/loading
- ~~Make it possible to update the pico via SD card~~ (drop `PICOSTATION.UF2` in the SD root and power on to auto-update).

### Links
- Original repo this fork is based on: https://github.com/paulocode/picostation
- The cue parser used in this project was written by Nicolas "Pixel" Noble, and is part of the PCSX-Redux repo: https://github.com/grumpycoders/pcsx-redux/tree/main/third_party/cueparser
- The SD card driver is by carlk3 and can be found here: https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
- Raspberry Pi Pico Visual Studio Code extension: https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico
- psx.dev discord server: https://discord.gg/QByKPpH
- PCB: https://github.com/paulocode/picostation_pcb
- FAQ: https://github.com/paulocode/picostation_faq
- Slow Solder Board (SSB) solder points / checking connection: https://web.archive.org/web/20230223033837/https://mmmonkey.co.uk/xstation-sony-playstation-install-notes-and-pinout/
- How to compile (Windows): https://shawnhymel.com/2096/how-to-set-up-raspberry-pi-pico-c-c-toolchain-on-windows-with-vs-code/
- PCB pinout: <a href="https://i.ibb.co/RvjvDyp/pinout.png"><img src="https://i.ibb.co/mDNDc8C/pinout.png" alt="pinout" border="0"></a>
- 3D Printable mount (550X) by <a href="https://twitter.com/SadSnifit">@Sadsnifit</a> : https://www.printables.com/fr/model/407224-picostation-mount-for-scph-5502
