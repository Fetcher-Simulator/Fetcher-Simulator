from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest


SERVICE_ROOT = Path(__file__).resolve().parents[1]


def request_json(url: str, body: dict | None = None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"} if data is not None else {},
        method="POST" if data is not None else "GET",
    )
    with urllib.request.urlopen(request, timeout=1) as response:
        return response.status, json.load(response)


def unused_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def start_master(port: int) -> subprocess.Popen:
    environment = os.environ.copy()
    environment.update(
        {
            "HEARTBEAT_TIMEOUT": "2",
            "REGISTER_ENDPOINT_RATE_SECONDS": "0",
            "SUPPORTED_PROTOCOL_VERSIONS": "1",
        }
    )
    process = subprocess.Popen(
        [
            sys.executable,
            "-m",
            "uvicorn",
            "master_server:app",
            "--host",
            "127.0.0.1",
            "--port",
            str(port),
            "--workers",
            "1",
            "--no-proxy-headers",
        ],
        cwd=SERVICE_ROOT,
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    wait_until(lambda: request_json(f"http://127.0.0.1:{port}/health")[0] == 200)
    return process


def stop_master(process: subprocess.Popen) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def wait_until(predicate, timeout: float = 10) -> None:
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, urllib.error.URLError) as error:
            last_error = error
        time.sleep(0.1)
    raise AssertionError(f"condition not met before timeout: {last_error}")


class SimulatedDedicatedServer:
    def __init__(self, base_url: str):
        self.base_url = base_url
        self.current_players = 0
        self.token = ""
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=5)
        if self.token:
            try:
                request_json(
                    f"{self.base_url}/v1/servers/unregister",
                    {"token": self.token},
                )
            except (OSError, urllib.error.URLError):
                pass

    def register(self) -> None:
        _, response = request_json(
            f"{self.base_url}/v1/servers/register",
            {
                "name": "Integration Server",
                "port": 25565,
                "max_players": 8,
                "build_version": "integration",
                "protocol_version": 1,
                "game_mode": "Test",
            },
        )
        self.token = response["token"]

    def run(self) -> None:
        while not self.stop_event.is_set():
            try:
                if not self.token:
                    self.register()
                request_json(
                    f"{self.base_url}/v1/servers/heartbeat",
                    {
                        "token": self.token,
                        "current_players": self.current_players,
                    },
                )
            except urllib.error.HTTPError as error:
                if error.code in (401, 404):
                    self.token = ""
            except (OSError, urllib.error.URLError):
                pass
            self.stop_event.wait(0.2)


@pytest.mark.integration
def test_server_recovers_after_master_restart_and_unregisters():
    port = unused_port()
    base_url = f"http://127.0.0.1:{port}"
    master = start_master(port)
    simulated = SimulatedDedicatedServer(base_url)
    simulated.start()
    try:
        wait_until(lambda: len(request_json(f"{base_url}/v1/servers")[1]) == 1)
        simulated.current_players = 3
        wait_until(
            lambda: request_json(f"{base_url}/v1/servers")[1][0]["current_players"]
            == 3
        )

        stop_master(master)
        master = start_master(port)
        wait_until(lambda: len(request_json(f"{base_url}/v1/servers")[1]) == 1)

        simulated.stop()
        wait_until(lambda: request_json(f"{base_url}/v1/servers")[1] == [])
    finally:
        simulated.stop()
        stop_master(master)
