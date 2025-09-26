#!/usr/bin/env bash
set -e  # stop on first error

# Detect if we're currently in the build folder
if [ "$(basename "$PWD")" = "build" ]; then
  echo "Inside build folder, moving up..."
  cd ..
fi

# Remove and recreate build folder
echo "Cleaning build folder..."
rm -rf build
mkdir build
cd build

# Configure with cmake
echo "Running cmake..."
cmake ..

# Use nproc if available, otherwise sysctl for macOS
if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=$(sysctl -n hw.ncpu)
fi

# Compile
echo "Building with $JOBS threads..."
make -j"$JOBS"

echo "✅ Build complete."
