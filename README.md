# Picostation

Forked from https://github.com/paulocode/picostation

I've taken some pretty drastic design choices with my fork of this project, such as restructuring most of the code and converting it to C++. Keeping the repo as a fork no longer made sense to me; the original repo is archived and inactive, so there would be no point in further syncing changes between the repos. I also found some of the limitations of a forked repo on github a bit annoying.

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

### Updating firmware from the SD card
1. Copy the new firmware image (`.uf2`) to the root of the Picostation SD card. If you have more than one UF2 on the card, the device will prefer a file named `picostation.uf2`; otherwise it will fall back to the most recently modified UF2.
2. Insert the card and power up the console.
3. Hold the Picostation reset button for about four seconds and then release it. A short press still performs a normal reset, and a one-second hold opens the file browser.
4. The status LED will light up solid while the UF2 is being programmed. Keep the console powered during this step. Once flashing succeeds the board reboots automatically into the new firmware.
5. If no suitable UF2 is found, the unit will return to normal operation after the next reset.


### To-do
- ~~Stabilize image loading~~
- Make an interface for image choice/loading
- ~~Make it possible to update the pico via SD card~~ <- Maybe this eventually, but not really a priority for me at the moment.

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
