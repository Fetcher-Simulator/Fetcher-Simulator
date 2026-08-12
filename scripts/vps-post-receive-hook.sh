#!/usr/bin/env bash
set -euo pipefail

zero_rev="0000000000000000000000000000000000000000"
bare_repo="${OPENMW_BARE_REPO:-/home/ubuntu/openmw-bare.git}"
work_tree="${OPENMW_VPS_WORK_TREE:-/root/openmw}"

while read -r oldrev newrev ref; do
    if [[ "$ref" != refs/heads/* ]]; then
        echo "Skipping non-branch ref: $ref"
        continue
    fi

    branch="${ref#refs/heads/}"

    if [[ "$newrev" == "$zero_rev" ]]; then
        echo "Skipping deleted branch: $branch"
        continue
    fi

    echo "==============================================="
    echo "Receiving code (branch: $branch)... starting deployment!"
    echo "==============================================="

    sudo -n git --work-tree="$work_tree" --git-dir="$bare_repo" checkout -f "$branch"
    sudo -n bash "$work_tree/scripts/vps-deploy-server.sh" "$work_tree"
done
