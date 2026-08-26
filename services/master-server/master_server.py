"""OpenMW multiplayer master-server registry."""

from __future__ import annotations

import ipaddress
import os
import re
import threading
import time
import uuid
from collections import defaultdict, deque
from dataclasses import dataclass
from typing import Callable

from fastapi import FastAPI, HTTPException, Query, Request, Response, status
from pydantic import BaseModel, ConfigDict, Field, field_validator

try:
    import geoip2.database
except ImportError:
    geoip2 = None


TOKEN_PATTERN = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
)


def _env_int(name: str, default: int, minimum: int = 0) -> int:
    value = int(os.getenv(name, str(default)))
    if value < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return value


@dataclass(frozen=True)
class Settings:
    heartbeat_timeout: int
    max_players: int
    endpoint_rate_seconds: int
    ip_rate_window_seconds: int
    ip_rate_max: int
    list_cache_seconds: int
    supported_protocols: frozenset[int]
    trusted_proxy_networks: tuple[ipaddress.IPv4Network | ipaddress.IPv6Network, ...]
    geoip_db_path: str | None

    @classmethod
    def from_env(cls) -> "Settings":
        protocols = frozenset(
            int(value.strip())
            for value in os.getenv("SUPPORTED_PROTOCOL_VERSIONS", "1").split(",")
            if value.strip()
        )
        if not protocols or any(value <= 0 for value in protocols):
            raise ValueError("SUPPORTED_PROTOCOL_VERSIONS must contain positive integers")

        proxy_networks = tuple(
            ipaddress.ip_network(value.strip(), strict=False)
            for value in os.getenv("TRUSTED_PROXY_NETWORKS", "").split(",")
            if value.strip()
        )
        max_players = _env_int("MAX_PLAYERS", 4096, 1)
        if max_players > 4096:
            raise ValueError("MAX_PLAYERS cannot exceed 4096")
        return cls(
            heartbeat_timeout=_env_int("HEARTBEAT_TIMEOUT", 90, 1),
            max_players=max_players,
            endpoint_rate_seconds=_env_int("REGISTER_ENDPOINT_RATE_SECONDS", 2),
            ip_rate_window_seconds=_env_int("REGISTER_IP_RATE_WINDOW_SECONDS", 60, 1),
            ip_rate_max=_env_int("REGISTER_IP_RATE_MAX", 20, 1),
            list_cache_seconds=_env_int("LIST_CACHE_SECONDS", 10),
            supported_protocols=protocols,
            trusted_proxy_networks=proxy_networks,
            geoip_db_path=os.getenv("GEOIP_DB_PATH") or None,
        )


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class RegisterRequest(StrictModel):
    name: str = Field(min_length=1, max_length=64)
    port: int = Field(ge=1, le=65535)
    max_players: int = Field(ge=1)
    build_version: str = Field(min_length=1, max_length=64)
    protocol_version: int = Field(gt=0)
    game_mode: str = Field(min_length=1, max_length=32)
    lan_address: str | None = Field(default=None, min_length=2, max_length=45)

    @field_validator("name", "build_version", "game_mode")
    @classmethod
    def strip_nonempty(cls, value: str) -> str:
        value = value.strip()
        if not value:
            raise ValueError("must not be empty")
        return value

    @field_validator("lan_address")
    @classmethod
    def valid_lan_address(cls, value: str | None) -> str | None:
        if value is None:
            return None
        address = ipaddress.ip_address(value.strip())
        if not (address.is_private or address.is_loopback):
            raise ValueError("lan_address must be a private or loopback IP address")
        return str(address)


class TokenRequest(StrictModel):
    token: str = Field(min_length=36, max_length=36)

    @field_validator("token")
    @classmethod
    def valid_token(cls, value: str) -> str:
        if not TOKEN_PATTERN.fullmatch(value):
            raise ValueError("invalid token format")
        return value


class HeartbeatRequest(TokenRequest):
    current_players: int = Field(ge=0)


class RegistrationResponse(StrictModel):
    id: str
    token: str


class OkResponse(StrictModel):
    ok: bool = True


class PublicServerEntry(StrictModel):
    id: str
    name: str
    host: str
    port: int
    current_players: int
    max_players: int
    build_version: str
    protocol_version: int
    game_mode: str
    country: str


class HealthResponse(StrictModel):
    status: str
    active_servers: int


@dataclass
class ServerRecord:
    id: str
    token: str
    name: str
    host: str
    port: int
    current_players: int
    max_players: int
    build_version: str
    protocol_version: int
    game_mode: str
    country: str
    lan_address: str | None
    last_heartbeat: float

    def public(self, requester_ip: str | None = None) -> PublicServerEntry:
        return PublicServerEntry(
            id=self.id,
            name=self.name,
            host=(
                self.lan_address
                if self.lan_address is not None and requester_ip == self.host
                else self.host
            ),
            port=self.port,
            current_players=self.current_players,
            max_players=self.max_players,
            build_version=self.build_version,
            protocol_version=self.protocol_version,
            game_mode=self.game_mode,
            country=self.country,
        )


class CountryLookup:
    def __init__(self, database_path: str | None):
        self._reader = None
        if database_path and geoip2 is not None:
            try:
                self._reader = geoip2.database.Reader(database_path)
            except OSError:
                pass

    def __call__(self, address: str) -> str:
        if self._reader is None:
            return "??"
        try:
            return self._reader.country(address).country.iso_code or "??"
        except Exception:
            return "??"


class Registry:
    def __init__(
        self,
        settings: Settings,
        *,
        clock: Callable[[], float] = time.monotonic,
        country_lookup: Callable[[str], str] | None = None,
    ):
        self.settings = settings
        self._clock = clock
        self._country_lookup = country_lookup or CountryLookup(settings.geoip_db_path)
        self._lock = threading.Lock()
        self._servers: dict[str, ServerRecord] = {}
        self._endpoint_tokens: dict[tuple[str, int], str] = {}
        self._last_endpoint_register: dict[tuple[str, int], float] = {}
        self._ip_registers: dict[str, deque[float]] = defaultdict(deque)

    def _expire_locked(self, now: float) -> None:
        expired = [
            token
            for token, record in self._servers.items()
            if now - record.last_heartbeat > self.settings.heartbeat_timeout
        ]
        for token in expired:
            record = self._servers.pop(token)
            self._endpoint_tokens.pop((record.host, record.port), None)

    def _prune_rate_limits_locked(self, now: float) -> None:
        endpoint_cutoff = now - self.settings.endpoint_rate_seconds
        for endpoint, registered_at in list(self._last_endpoint_register.items()):
            if registered_at <= endpoint_cutoff:
                del self._last_endpoint_register[endpoint]

        ip_cutoff = now - self.settings.ip_rate_window_seconds
        for source_ip, attempts in list(self._ip_registers.items()):
            while attempts and attempts[0] <= ip_cutoff:
                attempts.popleft()
            if not attempts:
                del self._ip_registers[source_ip]

    def register(self, source_ip: str, request: RegisterRequest) -> ServerRecord:
        if request.max_players > self.settings.max_players:
            raise HTTPException(
                422,
                f"max_players cannot exceed {self.settings.max_players}",
            )
        if request.protocol_version not in self.settings.supported_protocols:
            raise HTTPException(
                422,
                "unsupported protocol_version",
            )

        endpoint = (source_ip, request.port)
        now = self._clock()
        with self._lock:
            self._expire_locked(now)
            self._prune_rate_limits_locked(now)

            last_endpoint = self._last_endpoint_register.get(endpoint)
            if (
                last_endpoint is not None
                and now - last_endpoint < self.settings.endpoint_rate_seconds
            ):
                raise HTTPException(
                    status.HTTP_429_TOO_MANY_REQUESTS,
                    "too many registration attempts for this endpoint",
                )

            ip_attempts = self._ip_registers[source_ip]
            if len(ip_attempts) >= self.settings.ip_rate_max:
                raise HTTPException(
                    status.HTTP_429_TOO_MANY_REQUESTS,
                    "too many registration attempts from this address",
                )

            old_token = self._endpoint_tokens.get(endpoint)
            if old_token is not None:
                self._servers.pop(old_token, None)

            token = str(uuid.uuid4())
            record = ServerRecord(
                id=str(uuid.uuid4()),
                token=token,
                name=request.name,
                host=source_ip,
                port=request.port,
                current_players=0,
                max_players=request.max_players,
                build_version=request.build_version,
                protocol_version=request.protocol_version,
                game_mode=request.game_mode,
                country=self._country_lookup(source_ip),
                lan_address=request.lan_address,
                last_heartbeat=now,
            )
            self._servers[token] = record
            self._endpoint_tokens[endpoint] = token
            self._last_endpoint_register[endpoint] = now
            ip_attempts.append(now)
            return record

    def heartbeat(self, request: HeartbeatRequest) -> None:
        now = self._clock()
        with self._lock:
            self._expire_locked(now)
            record = self._servers.get(request.token)
            if record is None:
                raise HTTPException(status.HTTP_404_NOT_FOUND, "unknown token")
            if request.current_players > record.max_players:
                raise HTTPException(
                    422,
                    "current_players cannot exceed registered max_players",
                )
            record.current_players = request.current_players
            record.last_heartbeat = now

    def unregister(self, token: str) -> None:
        with self._lock:
            record = self._servers.pop(token, None)
            if record is not None:
                self._endpoint_tokens.pop((record.host, record.port), None)

    def list(
        self,
        protocol_version: int | None = None,
        requester_ip: str | None = None,
    ) -> list[PublicServerEntry]:
        now = self._clock()
        with self._lock:
            self._expire_locked(now)
            result = [
                record.public(requester_ip)
                for record in self._servers.values()
                if protocol_version is None
                or record.protocol_version == protocol_version
            ]
        result.sort(key=lambda entry: (-entry.current_players, entry.name, entry.id))
        return result

    def count(self) -> int:
        return len(self.list())


def _socket_ip(request: Request) -> ipaddress.IPv4Address | ipaddress.IPv6Address:
    host = request.client.host if request.client else "0.0.0.0"
    try:
        return ipaddress.ip_address(host)
    except ValueError:
        return ipaddress.ip_address("0.0.0.0")


def client_ip(request: Request, settings: Settings) -> str:
    peer = _socket_ip(request)
    if any(peer in network for network in settings.trusted_proxy_networks):
        forwarded = request.headers.get("X-Forwarded-For", "")
        candidate = forwarded.split(",", 1)[0].strip()
        try:
            return str(ipaddress.ip_address(candidate))
        except ValueError:
            pass
    return str(peer)


def create_app(
    settings: Settings | None = None,
    *,
    registry: Registry | None = None,
) -> FastAPI:
    settings = settings or Settings.from_env()
    registry = registry or Registry(settings)
    app = FastAPI(
        title="OpenMW Multiplayer Master Server",
        version="1.0.0",
        description="Single-worker public server registry.",
    )
    app.state.registry = registry
    app.state.settings = settings

    @app.get(
        "/v1/servers",
        response_model=list[PublicServerEntry],
        name="list_servers_v1",
    )
    def list_servers(
        request: Request,
        response: Response,
        protocol_version: int | None = Query(default=None, gt=0),
    ) -> list[PublicServerEntry]:
        response.headers["Cache-Control"] = (
            f"private, max-age={settings.list_cache_seconds}"
        )
        return registry.list(protocol_version, client_ip(request, settings))

    @app.post(
        "/v1/servers/register",
        response_model=RegistrationResponse,
        status_code=status.HTTP_201_CREATED,
        name="register_server_v1",
    )
    def register_server(body: RegisterRequest, request: Request) -> RegistrationResponse:
        record = registry.register(client_ip(request, settings), body)
        return RegistrationResponse(id=record.id, token=record.token)

    @app.post(
        "/v1/servers/heartbeat",
        response_model=OkResponse,
        name="heartbeat_v1",
    )
    def heartbeat(body: HeartbeatRequest) -> OkResponse:
        registry.heartbeat(body)
        return OkResponse()

    @app.post(
        "/v1/servers/unregister",
        response_model=OkResponse,
        name="unregister_v1",
    )
    def unregister(body: TokenRequest) -> OkResponse:
        registry.unregister(body.token)
        return OkResponse()

    @app.get("/health", response_model=HealthResponse)
    def health() -> HealthResponse:
        return HealthResponse(status="ok", active_servers=registry.count())

    # Temporary compatibility aliases for older dedicated servers and browsers.
    app.add_api_route(
        "/servers",
        list_servers,
        methods=["GET"],
        response_model=list[PublicServerEntry],
        include_in_schema=False,
    )
    app.add_api_route(
        "/register",
        register_server,
        methods=["POST"],
        response_model=RegistrationResponse,
        status_code=status.HTTP_201_CREATED,
        include_in_schema=False,
    )
    app.add_api_route(
        "/heartbeat",
        heartbeat,
        methods=["POST"],
        response_model=OkResponse,
        include_in_schema=False,
    )
    app.add_api_route(
        "/unregister",
        unregister,
        methods=["POST"],
        response_model=OkResponse,
        include_in_schema=False,
    )
    return app


app = create_app()


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        app,
        host=os.getenv("BIND_HOST", "0.0.0.0"),
        port=_env_int("BIND_PORT", 8080, 1),
        proxy_headers=False,
        workers=1,
    )
