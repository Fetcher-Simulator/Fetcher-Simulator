# OpenMW Multiplayer Server Setup Guide

This guide covers a complete basic deployment of this multiplayer `openmw-server`, from opening the network port through building and loading authoritative client content.

The included `Build-OpenMWServerAuthority.py` tool is generic to this multiplayer server branch. It does not contain a hard-coded mod list. The source client must contain a valid `openmw.cfg`, its configured data directories, and a matching `resources` directory. The client and server distributions should come from compatible builds so their built-in OpenMW resources agree.

## 1. What you need

Use a complete server distribution rather than copying only `openmw-server.exe` or the Linux binary. The server directory should contain the server executable plus the matching OpenMW runtime files such as `resources/` and `defaults.bin`.

You also need one **known-good client installation** that represents exactly what players are expected to run. Install all required mods, apply compatibility patches, and verify the client's `openmw.cfg` before generating server authority from it.

Python 3.10 or newer is required to run `Build-OpenMWServerAuthority.py`.

In Windows portable releases and macOS disk images, the builder and this guide are shipped at the top level of the release beside the other documentation. In the Ubuntu package they are installed under:

```text
/usr/share/games/openmw/server-tools/
```

## 2. Choose the server port

The default multiplayer port is UDP `25565`, but you can choose another unused UDP port if you prefer.

Example `server.cfg`:

```ini
[server]
port = 25565
max_players = 32
db_path = playerdata.db
```

The game server uses **UDP**. Forwarding only TCP 25565 will not make the game server reachable.

## 3. Give the server a stable LAN address

For a home-hosted server, configure a DHCP reservation in the router or otherwise give the host a stable private IPv4 address, for example:

```text
192.168.1.50
```

The router's port-forward rule must always point to the same machine.

## 4. Open the host firewall

On Windows, an Administrator PowerShell can create an inbound rule like:

```powershell
New-NetFirewallRule `
  -DisplayName "OpenMW Multiplayer UDP 25565" `
  -Direction Inbound `
  -Protocol UDP `
  -LocalPort 25565 `
  -Action Allow
```

On a Linux host using UFW:

```bash
sudo ufw allow 25565/udp
```

If you choose another server port, substitute that port everywhere.

## 5. Configure router port forwarding

Create a NAT/port-forward rule on the router:

```text
Protocol:      UDP
External port: 25565
Internal IP:   192.168.1.50
Internal port: 25565
```

Do not expose unrelated services or the player database to the Internet.

If the router's WAN address is itself private, or the WAN address does not match the public IPv4 address seen from the Internet, the connection may be behind double NAT or carrier-grade NAT. In that case normal port forwarding may not be sufficient; use a real public IPv4 address, correct the upstream NAT, host on a VPS, or use a UDP-capable tunnel/VPN designed for inbound hosting.

UDP port-check websites are often unreliable. The most useful test is an actual client connection from outside the host's LAN, such as a phone hotspot or another Internet connection.

## 6. Create `server.cfg`

`openmw-server` looks for `server.cfg` next to the executable unless `--config` is supplied. Portable Windows/server-directory deployments can keep the config beside the executable. System-wide Linux packages should normally keep a writable/operator-managed config elsewhere and start the server with an explicit `--config` path.

A practical production configuration is:

```ini
# OpenMW Multiplayer - Server Configuration

[server]
port = 25565
max_players = 32
db_path = playerdata.db
actorAuthorityExteriorRadius = 1
actorAuthorityStickyMs = 3000
actorAuthorityPreferExactCell = true

[content]
openmw_cfg = content-authority/openmw.cfg
# resources = /usr/share/games/openmw/resources   # useful for system-wide Linux installs
encoding = win1252
verify_determinism = true

[master]
public_listing = false
server_name = My OpenMW Server
game_mode = Co-op
lan_address =
```

The `openmw_cfg` path is resolved relative to the directory containing `server.cfg`. If `resources` is omitted, the server uses a `resources` directory beside the server executable. A relative `resources` value is also resolved from the `server.cfg` directory.

Authoritative content is mandatory in this server branch. Removing `content.openmw_cfg` does **not** disable validation; the server will refuse to start.

## 7. Build server content authority from a known-good client

Run the authority builder against the client installation that players are supposed to match.

Windows example:

```powershell
python .\Build-OpenMWServerAuthority.py `
  --client-root "C:\Games\OpenMW-MP" `
  --output "C:\OpenMW-Server\content-authority"
```

Linux portable-directory example:

```bash
python3 ./Build-OpenMWServerAuthority.py \
  --client-root /home/user/openmw-client \
  --output /opt/openmw-server/content-authority
```

Ubuntu package example:

```bash
python3 /usr/share/games/openmw/server-tools/Build-OpenMWServerAuthority.py \
  --client-root /home/user/openmw-client \
  --server-vfs-mw /usr/share/games/openmw/resources/vfs-mw \
  --output /var/lib/openmw-server/content-authority
```

For that Ubuntu layout, configure `server.cfg` with an absolute authority path and the packaged OpenMW resources:

```ini
[content]
openmw_cfg = /var/lib/openmw-server/content-authority/openmw.cfg
resources = /usr/share/games/openmw/resources
encoding = win1252
verify_determinism = true
```

To inspect the source installation without writing a bundle:

```powershell
python .\Build-OpenMWServerAuthority.py `
  --client-root "C:\Games\OpenMW-MP" `
  --plan-only
```

To replace an existing generated authority only after a successful new build:

```powershell
python .\Build-OpenMWServerAuthority.py `
  --client-root "C:\Games\OpenMW-MP" `
  --output "C:\OpenMW-Server\content-authority" `
  --force
```

Useful additional options are:

- `--strict-assets` — fail if a configured binary content file cannot be scanned for model/icon references.
- `--include-sounds` — include effective loose `sound/` files when runtime-created dialogue records need loose audio validation.
- `--openmw-cfg <path>` — use a configuration other than `<client-root>/openmw.cfg`.
- `--server-vfs-mw <path>` — write a different server-side `resources/vfs-mw` path into the generated authority config; useful for system-wide Linux packages.

## 8. What the authority builder creates

The generated directory looks like:

```text
content-authority/
├── openmw.cfg
├── bundle-manifest.json
└── content-data/
    ├── Morrowind.esm
    ├── Tribunal.esm
    ├── Bloodmoon.esm
    ├── *.esp
    ├── *.omwaddon
    ├── *.omwscripts
    ├── scripts/
    ├── meshes/
    ├── icons/
    └── ...
```

The builder resolves the source client's ordered `data=` stack, copies the effective files **byte-for-byte**, and generates an authority `openmw.cfg`. In addition to content files, Lua, and runtime-item assets, it scans TES3 `CELL` references and includes loose meshes for placed `STAT`, `ACTI`, `CONT`, `DOOR`, and `LIGH` records used by the server's authoritative collision/line-of-sight world. Unplaced scenery meshes are not copied, and archive-backed models remain available through the configured `fallback-archive=` files.

Because collision geometry must exist on the server, heavily modded worldspaces with many loose placed meshes can make `content-authority` substantially larger than a handshake-only bundle. The `collision-model` category and collision counts in `bundle-manifest.json` make that cost auditable.

By default its data paths are:

```ini
data="../resources/vfs-mw"
data="./content-data"
```

The first path can be changed with `--server-vfs-mw` for layouts where the server's `resources/vfs-mw` is not a sibling of `content-authority`. The second path always points at the generated byte-exact `content-data` directory.

It also carries across the source client's ordered `fallback-archive=`, `fallback=`, and `content=` entries.

The builder deliberately does not normalize Lua text, convert line endings, or add/remove UTF-8 BOMs. The multiplayer handshake hashes exact VFS bytes, so two Lua files with identical source text can still be different authoritative files if their byte representation differs.

The generated `bundle-manifest.json` is an audit record of what the Python tool copied. **The server does not load this JSON file as its handshake manifest.**

## 9. How the server creates the real manifest

At startup the server reads:

```text
server.cfg
    -> [content] openmw_cfg
    -> content-authority/openmw.cfg
    -> content-authority/content-data + matching server resources
```

`ServerContentRegistry` then creates a headless OpenMW content environment, loads the configured content in order, resolves the Lua script list through the VFS, and hashes the exact content and Lua bytes it sees.

The resulting content-file list, Lua list, SHA-256 hashes, and resolved-content fingerprint live in server memory. They are regenerated every time the server starts.

When a client connects, the client independently hashes its own effective content. The server compares the two manifests before authentication/gameplay continues.

This is why a content-authority update requires a server restart.

## 10. Keep server resources compatible with the client build

The generated bundle intentionally references the server distribution's `resources/vfs-mw` rather than copying OpenMW's built-in resources into `content-data`. The default generated path is:

```text
../resources/vfs-mw
```

Use `--server-vfs-mw` when the deployed server resources live elsewhere, such as `/usr/share/games/openmw/resources/vfs-mw` in the Ubuntu package.

Use a server distribution built from the same compatible code/release family as the client distribution used to generate the authority. A mismatched built-in script or resource can change the Lua manifest or resolved content fingerprint even when all mods are identical.

## 11. Start the server

Windows:

```powershell
.\openmw-server.exe
```

or with an explicit config:

```powershell
.\openmw-server.exe --config .\server.cfg
```

Linux portable-directory deployment:

```bash
./openmw-server --config ./server.cfg
```

Ubuntu package deployment:

```bash
openmw-server --config /etc/openmw-server/server.cfg
```

The config path is only an example; choose an operator-writable location appropriate for your host.

A healthy authority startup includes a line similar to:

```text
[ContentRegistry] Loaded 82 ordered content files, 175 Lua scripts; resolved fingerprint=<sha256>
```

The exact counts depend on the server's mod list.

With `verify_determinism = true`, the content registry is constructed twice during startup and the server refuses to continue if the two independent snapshots differ.

## 12. Public server listing

To advertise the server through the configured master server:

```ini
[master]
public_listing = true
server_name = My OpenMW Server
game_mode = Co-op
```

Keep the `master_url` value supplied by the server distribution unless you intentionally operate against another compatible master service.

`public_listing = true` does not replace firewall/NAT configuration. Internet clients still need a reachable UDP game port.

`lan_address` can be set to the server's private address when the master/listing implementation needs to tell clients on the same NAT which LAN address to use.

## 13. Local development authority versus production authority

For development, the server may point directly at a local client's configuration:

```ini
[content]
openmw_cfg = ../../../my-client/openmw.cfg
```

That means the server uses that client installation's live data directories as its authority every time it starts. Editing a Lua file in that client immediately changes what the next server start considers authoritative.

For a VPS or public production server, use an isolated generated bundle instead:

```ini
[content]
openmw_cfg = content-authority/openmw.cfg
```

This makes the server's expected content explicit, portable, auditable, and independent of a developer's working client directory.

## 14. Updating mods or Lua safely

When the required client content changes:

1. Update one clean reference client completely.
2. Launch it locally if practical and confirm its mod load order is correct.
3. Close the client.
4. Run `Build-OpenMWServerAuthority.py` against that installation.
5. Review `bundle-manifest.json` and the builder summary.
6. Back up the current production `content-authority` directory.
7. Replace it with the newly generated directory.
8. Restart `openmw-server` so `ServerContentRegistry` rebuilds its in-memory authority.
9. Connect using the same known-good client.
10. Only then distribute/announce the client update to everyone else.

Do not manually edit individual SHA-256 values. The server derives them from the actual files.

## 15. Backups and process supervision

Before server upgrades, back up at least:

```text
server.cfg
playerdata.db
server-lua-storage.bin        # if present
content-authority/
server scripts/configuration
```

On Linux, run the server under a process supervisor such as systemd, tmux plus an intentional restart script, or another service manager. The supervisor should use the same working directory as the deployed server and should not silently swap content-authority directories while the process is running.

## 16. Common failures

### `Authoritative content is not configured`

`[content] openmw_cfg` is missing or empty in `server.cfg`.

### `Could not open server content configuration`

The configured `openmw_cfg` path does not exist from the server's point of view. Remember that relative paths are resolved relative to `server.cfg`.

### `Authoritative content mismatch`

A configured content file such as an ESM/ESP/OMWADDON/OMWSCRIPTS differs between the client and server.

### `Authoritative Lua content mismatch`

The effective Lua file selected by the client's VFS differs from the server's effective file. Compare the expected and actual SHA-256 values printed by recent builds of the server/client.

Check for:

- different mod versions;
- different `data=` ordering;
- a compatibility patch applied to only one side;
- CRLF versus LF;
- UTF-8 BOM versus no BOM;
- stale duplicate files in a higher-priority data directory.

### Resolved content fingerprint mismatch

The static content loaded into the client and server is not semantically identical even if a simple file checklist looks similar. Verify content order, server/client built-in resources, and the exact generated authority.

### Server is visible on LAN but not the Internet

Check the host firewall, router UDP forward, stable LAN address, double NAT, and CGNAT. Test with a real client from an external connection.

### Server appears in the browser but joining times out

Master listing succeeded, but the UDP game port is still not reachable or is forwarded to the wrong host/port.

## 17. Recommended production layout

A simple deployment layout is:

```text
openmw-server-root/
├── openmw-server(.exe)
├── server.cfg
├── defaults.bin
├── resources/
│   ├── vfs/
│   ├── vfs-mw/
│   └── ...
├── content-authority/
│   ├── openmw.cfg
│   ├── bundle-manifest.json
│   └── content-data/
├── server scripts/configuration
├── playerdata.db
└── logs/
```

With that layout the production `server.cfg` can use the portable path:

```ini
[content]
openmw_cfg = content-authority/openmw.cfg
```

That keeps client authority generation separate from server runtime state while still letting the server itself perform the final authoritative OpenMW/VFS resolution.

For the Ubuntu package, a system-oriented layout can instead keep immutable program resources under `/usr/share/games/openmw`, operator configuration under `/etc/openmw-server`, and mutable authority/database/log state under `/var/lib/openmw-server` and `/var/log/openmw-server`. In that layout use absolute `openmw_cfg`/`resources` values and generate the authority with `--server-vfs-mw /usr/share/games/openmw/resources/vfs-mw`.
