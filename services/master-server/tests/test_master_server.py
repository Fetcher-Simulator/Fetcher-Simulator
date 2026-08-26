from __future__ import annotations

import ipaddress
from dataclasses import replace

import pytest
from fastapi.testclient import TestClient

from master_server import Registry, Settings, create_app


class FakeClock:
    def __init__(self) -> None:
        self.value = 1000.0

    def __call__(self) -> float:
        return self.value

    def advance(self, seconds: float) -> None:
        self.value += seconds


def settings(*, trusted_proxy_networks=()) -> Settings:
    return Settings(
        heartbeat_timeout=10,
        max_players=64,
        endpoint_rate_seconds=0,
        ip_rate_window_seconds=60,
        ip_rate_max=20,
        list_cache_seconds=0,
        supported_protocols=frozenset({1, 2}),
        trusted_proxy_networks=trusted_proxy_networks,
        geoip_db_path=None,
    )


@pytest.fixture
def api():
    clock = FakeClock()
    config = settings()
    registry = Registry(config, clock=clock, country_lookup=lambda _: "US")
    app = create_app(config, registry=registry)
    with TestClient(app, client=("198.51.100.10", 50000)) as client:
        yield client, clock


def registration(**overrides):
    body = {
        "name": 'A "quoted" {server} \\ Ω',
        "port": 25565,
        "max_players": 32,
        "build_version": "0.51.0-dev",
        "protocol_version": 1,
        "game_mode": "Co-op",
    }
    body.update(overrides)
    return body


def register(client: TestClient, **overrides) -> dict:
    response = client.post("/v1/servers/register", json=registration(**overrides))
    assert response.status_code == 201, response.text
    return response.json()


def test_register_list_heartbeat_and_unregister(api):
    client, _ = api
    registered = register(client)

    listed = client.get("/v1/servers")
    assert listed.status_code == 200
    assert listed.json() == [
        {
            "id": registered["id"],
            "name": 'A "quoted" {server} \\ Ω',
            "host": "198.51.100.10",
            "port": 25565,
            "current_players": 0,
            "max_players": 32,
            "build_version": "0.51.0-dev",
            "protocol_version": 1,
            "game_mode": "Co-op",
            "country": "US",
        }
    ]

    heartbeat = client.post(
        "/v1/servers/heartbeat",
        json={"token": registered["token"], "current_players": 7},
    )
    assert heartbeat.status_code == 200
    assert client.get("/v1/servers").json()[0]["current_players"] == 7

    removed = client.post(
        "/v1/servers/unregister", json={"token": registered["token"]}
    )
    assert removed.status_code == 200
    assert client.get("/v1/servers").json() == []


def test_heartbeat_rejects_player_count_above_registered_max(api):
    client, _ = api
    registered = register(client, max_players=2)
    response = client.post(
        "/v1/servers/heartbeat",
        json={"token": registered["token"], "current_players": 3},
    )
    assert response.status_code == 422


def test_listing_expires_after_heartbeat_timeout(api):
    client, clock = api
    register(client)
    clock.advance(11)
    assert client.get("/v1/servers").json() == []


def test_duplicate_endpoint_replaces_previous_registration(api):
    client, _ = api
    first = register(client, name="First")
    second = register(client, name="Second")
    listed = client.get("/v1/servers").json()
    assert [entry["name"] for entry in listed] == ["Second"]

    stale_heartbeat = client.post(
        "/v1/servers/heartbeat",
        json={"token": first["token"], "current_players": 1},
    )
    assert stale_heartbeat.status_code == 404
    assert first["id"] != second["id"]


def test_multiple_endpoints_behind_one_address_can_register(api):
    client, _ = api
    register(client, name="First", port=25565)
    register(client, name="Second", port=25566)
    assert len(client.get("/v1/servers").json()) == 2


def test_lan_address_is_used_only_for_same_public_ip():
    config = settings()
    registry = Registry(config, country_lookup=lambda _: "US")
    app = create_app(config, registry=registry)
    with TestClient(app, client=("198.51.100.10", 50000)) as local_client:
        register(local_client, lan_address="192.168.1.236")
        response = local_client.get("/v1/servers")
        assert response.json()[0]["host"] == "192.168.1.236"
        assert response.headers["Cache-Control"].startswith("private")

    with TestClient(app, client=("203.0.113.20", 50000)) as remote_client:
        assert remote_client.get("/v1/servers").json()[0]["host"] == "198.51.100.10"


def test_broader_source_address_rate_limit():
    config = replace(settings(), ip_rate_max=2)
    app = create_app(config, registry=Registry(config, country_lookup=lambda _: "US"))
    with TestClient(app, client=("198.51.100.10", 50000)) as client:
        assert client.post(
            "/v1/servers/register", json=registration(port=25565)
        ).status_code == 201
        assert client.post(
            "/v1/servers/register", json=registration(port=25566)
        ).status_code == 201
        assert client.post(
            "/v1/servers/register", json=registration(port=25567)
        ).status_code == 429


@pytest.mark.parametrize(
    "overrides",
    [
        {"name": "   "},
        {"game_mode": "\t"},
        {"build_version": ""},
        {"port": 0},
        {"port": 65536},
        {"max_players": 65},
        {"protocol_version": 0},
        {"protocol_version": 99},
        {"lan_address": "8.8.8.8"},
    ],
)
def test_rejects_malformed_or_empty_metadata(api, overrides):
    client, _ = api
    response = client.post("/v1/servers/register", json=registration(**overrides))
    assert response.status_code == 422


def test_filters_by_protocol_version(api):
    client, _ = api
    register(client, name="Protocol one", port=25565, protocol_version=1)
    register(client, name="Protocol two", port=25566, protocol_version=2)
    listed = client.get("/v1/servers", params={"protocol_version": 2}).json()
    assert [entry["name"] for entry in listed] == ["Protocol two"]


def test_public_responses_never_expose_tokens_or_timestamps(api):
    client, _ = api
    registered = register(client)
    serialized = client.get("/v1/servers").text
    assert registered["token"] not in serialized
    assert "token" not in serialized
    assert "heartbeat" not in serialized
    assert "timestamp" not in serialized


def test_spoofed_forwarded_header_is_ignored_by_default(api):
    client, _ = api
    client.post(
        "/v1/servers/register",
        json=registration(),
        headers={"X-Forwarded-For": "203.0.113.77"},
    )
    assert client.get("/v1/servers").json()[0]["host"] == "198.51.100.10"


def test_forwarded_header_is_honored_for_configured_proxy():
    config = settings(
        trusted_proxy_networks=(ipaddress.ip_network("198.51.100.0/24"),)
    )
    registry = Registry(config, country_lookup=lambda _: "US")
    app = create_app(config, registry=registry)
    with TestClient(app, client=("198.51.100.10", 50000)) as client:
        response = client.post(
            "/v1/servers/register",
            json=registration(),
            headers={"X-Forwarded-For": "203.0.113.77"},
        )
        assert response.status_code == 201
        assert client.get("/v1/servers").json()[0]["host"] == "203.0.113.77"


def test_health_prunes_expired_servers(api):
    client, clock = api
    register(client)
    assert client.get("/health").json() == {
        "status": "ok",
        "active_servers": 1,
    }
    clock.advance(11)
    assert client.get("/health").json() == {
        "status": "ok",
        "active_servers": 0,
    }


def test_unknown_and_malformed_tokens(api):
    client, _ = api
    malformed = client.post(
        "/v1/servers/heartbeat",
        json={"token": "not-a-token", "current_players": 0},
    )
    assert malformed.status_code == 422

    unknown = client.post(
        "/v1/servers/heartbeat",
        json={
            "token": "00000000-0000-4000-8000-000000000000",
            "current_players": 0,
        },
    )
    assert unknown.status_code == 404
