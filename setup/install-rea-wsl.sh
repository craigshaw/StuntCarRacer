#!/usr/bin/env bash
# Run as the normal WSL user after installing JDK 21, unzip and procps.
set -euo pipefail
if [[ $(id -u) == 0 ]]; then
  echo 'Run this as the normal Linux account, not root.' >&2
  exit 1
fi
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
opt="$HOME/.local/opt"
cache="$HOME/.cache/amiga-re-setup"
node_name=node-v24.15.0-linux-x64
mkdir -p "$opt" "$cache" "$HOME/.local/bin"
if [[ ! -x "$opt/$node_name/bin/node" ]]; then
  curl --fail --location --retry 2 --max-time 240 \
    "https://nodejs.org/dist/v24.15.0/$node_name.tar.xz" -o "$cache/$node_name.tar.xz"
  curl --fail --location --retry 2 --max-time 60 \
    https://nodejs.org/dist/v24.15.0/SHASUMS256.txt -o "$cache/SHASUMS256.txt"
  (cd "$cache"; grep "  $node_name.tar.xz$" SHASUMS256.txt | sha256sum -c -)
  tar -xJf "$cache/$node_name.tar.xz" -C "$opt"
fi
export PATH="$opt/$node_name/bin:$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
for command in node npm npx; do
  destination="$HOME/.local/bin/$command"
  target="$opt/$node_name/bin/$command"
  if [[ ! -e "$destination" && ! -L "$destination" ]]; then
    ln -s "$target" "$destination"
  elif [[ $(readlink "$destination") != "$target" ]]; then
    echo "Existing user command differs: $destination" >&2
    exit 1
  fi
done
npm install --global --prefix "$HOME/.local" rea-agents@6.7.0

archive="$project_root/setup/upstream/ghidra_12.1.4_PUBLIC_20260921.zip"
printf '%s  %s\n' ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db "$archive" | sha256sum -c -
if [[ ! -d "$opt/ghidra_12.1.4_PUBLIC" ]]; then
  unzip -q "$archive" -d "$opt"
fi
export GHIDRA_INSTALL_DIR="$opt/ghidra_12.1.4_PUBLIC"
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
test -x "$JAVA_HOME/bin/javac"
test -x "$GHIDRA_INSTALL_DIR/Ghidra/Features/Decompiler/os/linux_x86_64/decompile"

cat > "$HOME/.local/bin/rea-wsl" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/.local/opt/node-v24.15.0-linux-x64/bin:$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
export GHIDRA_INSTALL_DIR="$HOME/.local/opt/ghidra_12.1.4_PUBLIC"
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
exec "$HOME/.local/opt/node-v24.15.0-linux-x64/bin/node" "$HOME/.local/lib/node_modules/rea-agents/scripts/rea.mjs" "$@"
WRAPPER
chmod 755 "$HOME/.local/bin/rea-wsl"
"$HOME/.local/bin/rea-wsl" doctor --provider ghidra --json > "$project_root/setup/rea-wsl-doctor.json"
echo "Installed Linux REA; provider report: $project_root/setup/rea-wsl-doctor.json"
