#!/usr/bin/env bash
echo "=== Entering BOOTSEL mode ==="

if ! command -v picotool &>/dev/null; then
    echo "⚠️  picotool not found. Install it from https://github.com/raspberrypi/picotool"
    exit 1
fi

if ! picotool reboot -f; then
    echo "⚠️  Failed to reboot into BOOTSEL. Is the Pico connected and running firmware with USB enabled?"
    exit 1
fi

echo "✅ Pico should now be in BOOTSEL mode. Copy your UF2 file to the RPI-RP2 drive."
