#!/usr/bin/env bash
set -euo pipefail

mode="apply"
if [[ "${1:-}" == "--check" ]]; then
    mode="check"
    shift
fi

repo_root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
container_name="${OPENMW_MASTER_CONTAINER_NAME:-openmw-master-server}"
rollback_name="${OPENMW_MASTER_ROLLBACK_NAME:-openmw-master-server-rollback}"
expected_image="${OPENMW_MASTER_IMAGE:-openmw-master-server:local}"
expected_restart="${OPENMW_MASTER_RESTART_POLICY:-unless-stopped}"
expected_bind="${OPENMW_MASTER_GEOIP_BIND:-/var/lib/openmw-master/geoip:/geoip:ro}"
expected_port="${OPENMW_MASTER_PORT_BINDING:-127.0.0.1:8080}"
expected_trusted_proxy="${OPENMW_MASTER_TRUSTED_PROXY_NETWORKS:-172.17.0.1/32}"
expected_geoip_path="${OPENMW_MASTER_GEOIP_DB_PATH:-/geoip/country.mmdb}"
protocol_header="$repo_root/components/openmw-mp/MasterServerProtocol.hpp"

fail()
{
    echo "ERROR: $*" >&2
    exit 1
}

if [[ ! -f "$protocol_header" ]]; then
    fail "multiplayer protocol header not found: $protocol_header"
fi

protocol_version="$(sed -nE 's/^[[:space:]]*inline constexpr int MultiplayerProtocolVersion = ([0-9]+);/\1/p' "$protocol_header" | head -n 1)"
if [[ ! "$protocol_version" =~ ^[0-9]+$ ]] || (( protocol_version < 1 )); then
    fail "could not parse MultiplayerProtocolVersion from $protocol_header"
fi

supported_versions="$(seq -s, 1 "$protocol_version")"

if (( EUID == 0 )); then
    docker_cmd=(docker)
else
    docker_cmd=(sudo -n docker)
fi

docker_cli()
{
    "${docker_cmd[@]}" "$@"
}

container_env()
{
    local key="$1"
    docker_cli inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$container_name" \
        | sed -n "s/^${key}=//p" \
        | head -n 1
}

wait_for_healthy()
{
    local name="$1"
    local state health
    for _ in $(seq 1 30); do
        state="$(docker_cli inspect -f '{{.State.Status}}' "$name" 2>/dev/null || true)"
        health="$(docker_cli inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$name" 2>/dev/null || true)"
        if [[ "$state" == "running" && "$health" == "healthy" ]]; then
            return 0
        fi
        if [[ "$state" == "exited" || "$state" == "dead" ]]; then
            return 1
        fi
        sleep 1
    done
    return 1
}

if ! docker_cli inspect "$container_name" >/dev/null 2>&1; then
    fail "master container '$container_name' does not exist; refusing automatic bootstrap"
fi

actual_image="$(docker_cli inspect -f '{{.Config.Image}}' "$container_name")"
actual_restart="$(docker_cli inspect -f '{{.HostConfig.RestartPolicy.Name}}' "$container_name")"
actual_binds="$(docker_cli inspect -f '{{json .HostConfig.Binds}}' "$container_name")"
actual_port="$(docker_cli port "$container_name" 8080/tcp | head -n 1 | tr -d '\r')"
actual_state="$(docker_cli inspect -f '{{.State.Status}}' "$container_name")"
actual_health="$(docker_cli inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$container_name")"
actual_supported="$(container_env SUPPORTED_PROTOCOL_VERSIONS)"
actual_trusted_proxy="$(container_env TRUSTED_PROXY_NETWORKS)"
actual_geoip_path="$(container_env GEOIP_DB_PATH)"

[[ "$actual_image" == "$expected_image" ]] \
    || fail "unexpected master image '$actual_image' (expected '$expected_image')"
[[ "$actual_restart" == "$expected_restart" ]] \
    || fail "unexpected master restart policy '$actual_restart' (expected '$expected_restart')"
[[ "$actual_binds" == "[\"$expected_bind\"]" ]] \
    || fail "unexpected master bind configuration '$actual_binds'"
[[ "$actual_port" == "$expected_port" ]] \
    || fail "unexpected master port binding '$actual_port' (expected '$expected_port')"
[[ "$actual_trusted_proxy" == "$expected_trusted_proxy" ]] \
    || fail "unexpected TRUSTED_PROXY_NETWORKS '$actual_trusted_proxy'"
[[ "$actual_geoip_path" == "$expected_geoip_path" ]] \
    || fail "unexpected GEOIP_DB_PATH '$actual_geoip_path'"
[[ "$actual_state" == "running" && "$actual_health" == "healthy" ]] \
    || fail "master container is not healthy (state=$actual_state health=$actual_health)"

if [[ "$actual_supported" == "$supported_versions" ]]; then
    echo "[MasterProtocol] already current: protocol=$protocol_version supported=$supported_versions"
    exit 0
fi

if [[ "$mode" == "check" ]]; then
    echo "[MasterProtocol] update required: current=$actual_supported target=$supported_versions"
    exit 0
fi

echo "[MasterProtocol] updating supported protocols: $actual_supported -> $supported_versions"

if docker_cli inspect "$rollback_name" >/dev/null 2>&1; then
    docker_cli rm -f "$rollback_name" >/dev/null
fi

docker_cli stop "$container_name" >/dev/null
docker_cli rename "$container_name" "$rollback_name"

restore_rollback()
{
    set +e
    echo "ERROR: new master container failed health validation; restoring rollback" >&2
    docker_cli logs --tail 100 "$container_name" >&2 2>/dev/null
    docker_cli rm -f "$container_name" >/dev/null 2>&1
    docker_cli rename "$rollback_name" "$container_name" >/dev/null 2>&1
    docker_cli start "$container_name" >/dev/null 2>&1
    if wait_for_healthy "$container_name"; then
        echo "[MasterProtocol] rollback restored and healthy" >&2
    else
        echo "ERROR: rollback container did not return healthy" >&2
    fi
    set -e
}

if ! docker_cli run -d \
    --name "$container_name" \
    --restart "$expected_restart" \
    -p "$expected_port:8080" \
    -v "$expected_bind" \
    -e "SUPPORTED_PROTOCOL_VERSIONS=$supported_versions" \
    -e "TRUSTED_PROXY_NETWORKS=$expected_trusted_proxy" \
    -e "GEOIP_DB_PATH=$expected_geoip_path" \
    "$expected_image" >/dev/null; then
    restore_rollback
    exit 1
fi

if ! wait_for_healthy "$container_name"; then
    restore_rollback
    exit 1
fi

echo "[MasterProtocol] healthy: protocol=$protocol_version supported=$supported_versions"
echo "[MasterProtocol] previous master retained as '$rollback_name'"
