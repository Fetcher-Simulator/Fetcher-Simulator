# OpenMW Multiplayer Master Server

The master server is a lightweight public server registry for the multiplayer
server browser. It requires Python 3.11 or newer.

## Run locally

```bash
python -m venv .venv
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/uvicorn master_server:app --host 127.0.0.1 --port 8080 --workers 1 --no-proxy-headers
```

On Windows, use `.venv\Scripts\python` and `.venv\Scripts\uvicorn`.

Run tests with:

```bash
python -m pytest
```

## API

- `GET /v1/servers`
- `GET /v1/servers?protocol_version=1`
- `POST /v1/servers/register`
- `POST /v1/servers/heartbeat`
- `POST /v1/servers/unregister`
- `GET /health`

Registration:

```json
{
  "name": "My Server",
  "port": 25565,
  "max_players": 32,
  "build_version": "0.51.0",
  "protocol_version": 1,
  "game_mode": "Co-op",
  "lan_address": "192.168.1.10"
}
```

The response contains an opaque public `id` and a private `token`. Heartbeats
and unregistration send the token. Public list entries never contain tokens,
heartbeat timestamps, or internal bookkeeping.

`lan_address` is optional and must be a private or loopback IP address. When a
browser request has the same public IP as the registered server, the master
returns this address in `host`; other clients continue to receive the public
source address. This avoids UDP NAT-loopback failures for players on the
server's LAN.

Legacy unversioned routes remain as temporary aliases.

## Environment

| Variable | Default | Description |
|---|---:|---|
| `BIND_HOST` | `0.0.0.0` | Bind address used by `python master_server.py` |
| `BIND_PORT` | `8080` | Bind port used by `python master_server.py` |
| `HEARTBEAT_TIMEOUT` | `90` | Seconds before a listing expires |
| `MAX_PLAYERS` | `4096` | Maximum accepted registered capacity |
| `SUPPORTED_PROTOCOL_VERSIONS` | `1` | Comma-separated positive protocol versions |
| `REGISTER_ENDPOINT_RATE_SECONDS` | `2` | Minimum interval per source IP and advertised port |
| `REGISTER_IP_RATE_WINDOW_SECONDS` | `60` | Broader source-IP rate-limit window |
| `REGISTER_IP_RATE_MAX` | `20` | Registrations allowed per source-IP window |
| `LIST_CACHE_SECONDS` | `10` | Reserved list-cache policy value |
| `TRUSTED_PROXY_NETWORKS` | empty | Comma-separated proxy IP networks allowed to supply `X-Forwarded-For` |
| `GEOIP_DB_PATH` | empty | Optional MaxMind-compatible Country MMDB database |

The production deployment at `master.fetchers.org` uses the
[DB-IP Country Lite database](https://db-ip.com/db/download/ip-to-country-lite),
licensed under CC BY 4.0. Country results are provided by DB-IP.com.

## Deployment constraints

The registry is intentionally in memory. Run exactly one Uvicorn worker.
Multiple workers or multiple service replicas are unsupported because they
would publish different server lists. Dedicated servers recover after a
service restart by detecting an unknown heartbeat token and registering again.

Run Uvicorn with `--no-proxy-headers`. The application ignores
`X-Forwarded-For` unless the socket peer belongs to `TRUSTED_PROXY_NETWORKS`.
When Nginx is on the same host, set this to `127.0.0.1/32,::1/128` and use the
provided Nginx configuration. Do not configure a broad network unless every
address in it is a controlled reverse proxy.

The production URL is `https://master.fetchers.org`. TLS terminates at Nginx;
the C++ clients validate its public certificate normally.

The Docker image and systemd unit both enforce one worker and disable Uvicorn's
own proxy-header rewriting. `/health` is suitable for liveness checks and also
reports the number of non-expired listings.
