#!/usr/bin/env bash
set -eu

echo "============================"
echo "      Installing vmd        "
echo "============================"

# Work from the source directory and install under ~/.local by default.
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SRC_DIR")"
PREFIX="${PREFIX:-$HOME/.local}"

# Pick a compiler from CC or the usual system names.
CC="${CC:-}"
if [ -z "$CC" ]; then
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CC="$candidate"
            break
        fi
    done
fi
if [ -z "$CC" ]; then
    echo "Error: no C compiler found. Install gcc or clang first." >&2
    exit 1
fi

# Build the binary into vmd-bin/ next to the source tree.
echo "[1/3] Building..."
mkdir -p "$ROOT/vmd-bin"
"$CC" -std=c11 -O2 -Wall -Wextra \
    -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE \
    "$SRC_DIR"/*.c -o "$ROOT/vmd-bin/vmd"

# Install the executable into PREFIX/bin.
echo "[2/3] Installing to $PREFIX/bin..."
mkdir -p "$PREFIX/bin"
install -m 755 "$ROOT/vmd-bin/vmd" "$PREFIX/bin/vmd"

# Add PREFIX/bin to the user's shell startup file when possible.
echo "[3/3] Configuring PATH..."
BIN_PATH="$PREFIX/bin"
SHELL_RC=""

case "${SHELL:-}" in
    */zsh) SHELL_RC="$HOME/.zshrc" ;;
    */bash) SHELL_RC="$HOME/.bashrc" ;;
esac

if [ -n "$SHELL_RC" ]; then
    if ! grep -Fq "$BIN_PATH" "$SHELL_RC" 2>/dev/null; then
        printf '\n# Added by vmd installer\nexport PATH="$PATH:%s"\n' "$BIN_PATH" >> "$SHELL_RC"
        echo "Added $BIN_PATH to $SHELL_RC"
    else
        echo "$BIN_PATH is already in $SHELL_RC"
    fi
else
    echo "Warning: could not detect your shell RC file."
    echo "Add $BIN_PATH to your PATH manually."
fi

echo ""
echo "=================================================="
echo " Installation completed successfully!"
echo " Run: source ${SHELL_RC:-your shell rc} then: vmd help"
echo "=================================================="
