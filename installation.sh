#!/bin/bash

echo "============================"
echo "    Installing vmd VCS      "
echo "============================"

# 1. Create the required directory structure
echo "[1/4] Creating directories..."
mkdir -p "$HOME/vmd/bin"
mkdir -p "$HOME/vmd/projects"

# 2. Compile the source code
echo "[2/4] Compiling source files..."
if ! command -v gcc &> /dev/null; then
    echo "Error: gcc is not installed. Please install a C compiler first."
    exit 1
fi

gcc *.c -o vmd
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed!"
    exit 1
fi

# 3. Move the compiled binary to ~/vmd/bin
echo "[3/4] Installing binary..."
mv vmd "$HOME/vmd/bin/"
chmod +x "$HOME/vmd/bin/vmd"

# 4. Add ~/vmd/bin to PATH
echo "[4/4] Configuring PATH..."
BIN_PATH="$HOME/vmd/bin"
SHELL_RC=""

if [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
elif [[ "$SHELL" == */bash ]]; then
    SHELL_RC="$HOME/.bashrc"
fi

if [ -n "$SHELL_RC" ]; then
    if ! grep -q "$BIN_PATH" "$SHELL_RC"; then
        echo "" >> "$SHELL_RC"
        echo "# Added by vmd installer" >> "$SHELL_RC"
        echo "export PATH=\"\$PATH:$BIN_PATH\"" >> "$SHELL_RC"
        echo "Added $BIN_PATH to $SHELL_RC"
    else
        echo "PATH is already configured in $SHELL_RC"
    fi
else
    echo "Warning: Could not detect shell RC file. Please add $BIN_PATH to your PATH manually."
fi

echo ""
echo "=================================================="
echo " Installation completed successfully!             "
echo " Please run: source ~/.bashrc (or ~/.zshrc)       "
echo " Then you can use 'vmd help' from anywhere.       "
echo "=================================================="