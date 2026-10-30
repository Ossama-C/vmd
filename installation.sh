#!/bin/sh
# Build vmd and install the binary in ~/.local/bin.

set -e

cd "$(dirname "$0")"

CC="${CC:-cc}"
$CC -std=c11 -O2 -Wall -Wextra *.c -o vmd

mkdir -p "$HOME/.local/bin"
cp vmd "$HOME/.local/bin/vmd"
chmod 755 "$HOME/.local/bin/vmd"

echo "vmd installed as $HOME/.local/bin/vmd"
echo "make sure $HOME/.local/bin is in your PATH"
