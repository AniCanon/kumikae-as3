#!/bin/sh
# Installs Meta's idb for input in the Simulator panel: the companion that talks to the Simulator, and
# the client the panel runs. idb uses private Apple interfaces; this is the owner's choice, and nothing
# is run without the owner's yes: the panel asks before it runs this, and it can be run by hand.
set -eu

command -v brew >/dev/null || { echo "Homebrew is needed for idb's companion: https://brew.sh" >&2; exit 1; }
brew tap facebook/fb
# Homebrew asks for a tap outside its own to be trusted; only this one formula is.
brew trust --formula facebook/fb/idb-companion 2>/dev/null || true
brew install facebook/fb/idb-companion

if command -v uv >/dev/null; then
    uv tool install fb-idb
elif command -v pipx >/dev/null; then
    pipx install fb-idb
else
    python3 -m venv "$HOME/.local/share/kumikae-idb"
    "$HOME/.local/share/kumikae-idb/bin/pip" install fb-idb
    mkdir -p "$HOME/.local/bin"
    ln -sf "$HOME/.local/share/kumikae-idb/bin/idb" "$HOME/.local/bin/idb"
fi

echo "idb is installed. In the Simulator panel, choose More > Switch input on."
