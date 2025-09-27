#!/usr/bin/env bash
set -e

# === CONFIG ===
TOOLCHAIN_VER="14.3.Rel1"
TOOLCHAIN_URL="https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-${TOOLCHAIN_VER}-x86_64-arm-none-eabi.tar.xz"
TOOLCHAIN_DIR="$HOME/arm-gnu-toolchain-${TOOLCHAIN_VER}-x86_64-arm-none-eabi"

install_packages() {
    echo "[*] Installing system packages..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            ninja-build \
            python3 \
            python3-pip \
            git \
            pkg-config \
            libusb-1.0-0-dev \
            xz-utils
    elif command -v brew &>/dev/null; then
        brew install cmake ninja python git pkg-config libusb xz
    else
        echo "[-] No supported package manager found (apt/brew). Install dependencies manually."
        exit 1
    fi

    echo "[*] Installing required Python packages..."
    python3 -m pip install --upgrade pip pyserial
}

install_toolchain() {
    if [ ! -d "$TOOLCHAIN_DIR" ]; then
        echo "[*] Downloading ARM toolchain $TOOLCHAIN_VER..."
        wget -q --show-progress "$TOOLCHAIN_URL" -O /tmp/arm-toolchain.tar.xz
        echo "[*] Extracting toolchain to $HOME..."
        tar -xf /tmp/arm-toolchain.tar.xz -C "$HOME"
    else
        echo "[*] ARM toolchain already present at $TOOLCHAIN_DIR"
    fi

    if ! grep -q "$TOOLCHAIN_DIR/bin" <<< "$PATH"; then
        echo "export PATH=\"$TOOLCHAIN_DIR/bin:\$PATH\"" >> ~/.bashrc
        echo "[*] Toolchain path added to ~/.bashrc (run 'source ~/.bashrc' or restart shell)"
    fi
}

init_git_submodules() {
    echo "[*] Initializing git submodules recursively..."
    git submodule update --init --recursive
}

install_packages
install_toolchain
init_git_submodules

echo "[+] Setup complete. Toolchain installed, PATH updated, and submodules synced."
echo "[+] You can now build with: mkdir -p build && cd build && cmake .. && make -j$(nproc)"
