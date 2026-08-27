#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-/root/openmw}"
dist_root="${OPENMW_SERVER_DIST_ROOT:-/root/openmw-server-dist}"
hook_path="${OPENMW_BARE_HOOK_PATH:-/home/ubuntu/openmw-bare.git/hooks/post-receive}"

cd "$repo_root"
export CC="${CC:-gcc-13}"
export CXX="${CXX:-g++-13}"

chmod +x ./scripts/build-server.sh ./scripts/update-master-protocol.sh ./scripts/vps-post-receive-hook.sh
./scripts/build-server.sh

built_binary=""
if [[ -f "$repo_root/build-linux/openmw-server" ]]; then
    built_binary="$repo_root/build-linux/openmw-server"
elif [[ -f /root/openmw-server-build/openmw-server ]]; then
    built_binary="/root/openmw-server-build/openmw-server"
else
    echo "ERROR: could not locate built openmw-server binary" >&2
    exit 1
fi

# Validate/update master-server protocol admission only after the build succeeds,
# but before staging the new game-server binary. A master configuration failure
# therefore aborts the deployment without changing the installed server binary.
"$repo_root/scripts/update-master-protocol.sh" "$repo_root"

mkdir -p "$dist_root/logs" "$dist_root/server-scripts" "$dist_root/server-lua-packages"
install -m 0755 "$built_binary" "$dist_root/openmw-server.next"
mv -f "$dist_root/openmw-server.next" "$dist_root/openmw-server"

if [[ -d "$repo_root/build-linux/server-scripts" ]]; then
    cp -a "$repo_root/build-linux/server-scripts/." "$dist_root/server-scripts/"
elif [[ -d /root/openmw-server-build/server-scripts ]]; then
    cp -a /root/openmw-server-build/server-scripts/. "$dist_root/server-scripts/"
fi

if [[ -d "$repo_root/build-linux/server-lua-packages" ]]; then
    rm -rf "$dist_root/server-lua-packages"
    cp -a "$repo_root/build-linux/server-lua-packages" "$dist_root/server-lua-packages"
elif [[ -d /root/openmw-server-build/server-lua-packages ]]; then
    rm -rf "$dist_root/server-lua-packages"
    cp -a /root/openmw-server-build/server-lua-packages "$dist_root/server-lua-packages"
fi

# Keep the live bare-repository hook synchronized with the tracked dispatcher.
install -m 0755 "$repo_root/scripts/vps-post-receive-hook.sh" "$hook_path"

echo "==============================================="
echo "Build complete. Binary updated in $dist_root/"
echo "Master protocol allowlist verified."
echo "==============================================="
