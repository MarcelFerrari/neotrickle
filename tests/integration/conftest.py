from __future__ import annotations

import hashlib
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable, Sequence
from urllib.parse import urlparse

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_NTRICKLE_BIN = REPO_ROOT / "build" / "bin" / "ntrickle"
DEFAULT_NTRICKLED_BIN = REPO_ROOT / "build" / "bin" / "ntrickled"
DEFAULT_PRELOAD_LIB = REPO_ROOT / "build" / "lib" / "libntrickle_preload.so"
DEFAULT_DOWNLOAD_LIMIT = "64K"
DEFAULT_WRAPPED_DOWNLOAD_LIMIT = "1M"
DEFAULT_DOWNLOAD_SIZE = 512 * 1024


@dataclass(frozen=True)
class DownloadFixture:
    size: int
    sha256: str
    url: str


@dataclass(frozen=True)
class CommandResult:
    stdout: bytes
    stderr: bytes
    returncode: int


@dataclass(frozen=True)
class PreparedCommand:
    argv: list[str]
    cwd: Path | None = None


@dataclass(frozen=True)
class NtricklePaths:
    ntrickle_bin: Path
    ntrickled_bin: Path
    preload_lib: Path


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--ntrickle-mode",
        choices=("plain", "wrapped", "both"),
        default="both",
        help="Run integration downloads directly, through ntrickle, or both.",
    )
    parser.addoption(
        "--ntrickle-bin",
        default=os.environ.get("NTRICKLE_BIN", str(DEFAULT_NTRICKLE_BIN)),
        help="Path to the built ntrickle binary for --ntrickle-mode=wrapped/both.",
    )
    parser.addoption(
        "--ntrickled-bin",
        default=os.environ.get("NTRICKLED_BIN", str(DEFAULT_NTRICKLED_BIN)),
        help="Path to the built ntrickled binary for daemon characterization tests.",
    )
    parser.addoption(
        "--ntrickle-preload-lib",
        default=os.environ.get("NTRICKLE_PRELOAD_LIB", str(DEFAULT_PRELOAD_LIB)),
        help="Path to the built libntrickle_preload.so for wrapped tests.",
    )


def pytest_generate_tests(metafunc: pytest.Metafunc) -> None:
    if "ntrickle_mode" not in metafunc.fixturenames:
        return

    selected = metafunc.config.getoption("--ntrickle-mode")
    modes = ("plain", "wrapped") if selected == "both" else (selected,)
    metafunc.parametrize("ntrickle_mode", modes)


def deterministic_bytes(size: int) -> bytes:
    pattern = bytes(range(256))
    full_chunks, remainder = divmod(size, len(pattern))
    return pattern * full_chunks + pattern[:remainder]


class LocalDownloadServer:
    def __init__(
        self,
        server: ThreadingHTTPServer,
        thread: threading.Thread,
        host: str = "127.0.0.1",
    ):
        self._server = server
        self._thread = thread
        port = server.server_address[1]
        if ":" in host:
            self.base_url = f"http://[{host}]:{port}"
        else:
            self.base_url = f"http://{host}:{port}"

    def bytes_url(self, size: int) -> str:
        return f"{self.base_url}/bytes/{size}"

    def shutdown(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=5)


class ByteStreamHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ntrickle-test"

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        parts = parsed.path.strip("/").split("/")

        if len(parts) != 2 or parts[0] != "bytes":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            size = int(parts[1])
        except ValueError:
            self.send_error(HTTPStatus.BAD_REQUEST)
            return

        if size < 0:
            self.send_error(HTTPStatus.BAD_REQUEST)
            return

        body = deterministic_bytes(size)
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def log_message(self, format: str, *args: object) -> None:
        return


class IPv6ThreadingHTTPServer(ThreadingHTTPServer):
    address_family = socket.AF_INET6


@pytest.fixture(scope="session")
def local_http_server() -> LocalDownloadServer:
    server = ThreadingHTTPServer(("127.0.0.1", 0), ByteStreamHandler)
    thread = threading.Thread(target=server.serve_forever, name="ntrickle-httpd")
    thread.daemon = True
    thread.start()

    fixture = LocalDownloadServer(server, thread)
    try:
        yield fixture
    finally:
        fixture.shutdown()


@pytest.fixture(scope="session")
def local_http_server_ipv6() -> LocalDownloadServer:
    try:
        server = IPv6ThreadingHTTPServer(("::1", 0), ByteStreamHandler)
    except OSError:
        pytest.skip("IPv6 loopback is not available")

    thread = threading.Thread(target=server.serve_forever, name="ntrickle-httpd6")
    thread.daemon = True
    thread.start()

    fixture = LocalDownloadServer(server, thread, host="::1")
    try:
        yield fixture
    finally:
        fixture.shutdown()


@pytest.fixture
def download_fixture(local_http_server: LocalDownloadServer) -> DownloadFixture:
    data = deterministic_bytes(DEFAULT_DOWNLOAD_SIZE)
    return DownloadFixture(
        size=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
        url=local_http_server.bytes_url(len(data)),
    )


@pytest.fixture
def ntrickle_paths(pytestconfig: pytest.Config) -> NtricklePaths:
    return NtricklePaths(
        ntrickle_bin=Path(pytestconfig.getoption("--ntrickle-bin")).resolve(),
        ntrickled_bin=Path(pytestconfig.getoption("--ntrickled-bin")).resolve(),
        preload_lib=Path(pytestconfig.getoption("--ntrickle-preload-lib")).resolve(),
    )


def require_ntrickle_paths(paths: NtricklePaths, *, include_daemon: bool = False) -> None:
    if not paths.ntrickle_bin.exists():
        pytest.fail(
            f"ntrickle binary not found at {paths.ntrickle_bin}. "
            "Build the project first or pass --ntrickle-bin."
        )
    if not paths.preload_lib.exists():
        pytest.fail(
            f"ntrickle preload library not found at {paths.preload_lib}. "
            "Build the project first or pass --ntrickle-preload-lib."
        )
    if include_daemon and not paths.ntrickled_bin.exists():
        pytest.fail(
            f"ntrickled binary not found at {paths.ntrickled_bin}. "
            "Build the project first or pass --ntrickled-bin."
        )


def build_wrapped_command(
    paths: NtricklePaths,
    command: Sequence[str],
    *,
    download_limit: str | None = None,
    standalone: bool = True,
    socket_path: Path | None = None,
) -> PreparedCommand:
    require_ntrickle_paths(paths)

    wrapper = [str(paths.ntrickle_bin)]
    if standalone:
        wrapper.extend(["-m", "standalone"])
    elif socket_path is not None:
        wrapper.extend(["-n", str(socket_path)])

    wrapper.extend(["-d", download_limit or DEFAULT_WRAPPED_DOWNLOAD_LIMIT])
    return PreparedCommand([*wrapper, "--", *command], cwd=paths.preload_lib.parent)


def wrapped_command(
    paths: NtricklePaths,
    ntrickle_mode: str,
    command: Sequence[str],
    download_limit: str | None,
) -> PreparedCommand:
    if ntrickle_mode == "plain":
        return PreparedCommand(list(command))

    return build_wrapped_command(
        paths,
        command,
        download_limit=download_limit,
        standalone=True,
    )


@pytest.fixture
def run_download_command(
    ntrickle_paths: NtricklePaths, ntrickle_mode: str
) -> Callable[..., CommandResult]:
    def run(
        command: Sequence[str],
        *,
        timeout: float = 20.0,
        download_limit: str | None = None,
    ) -> CommandResult:
        executable = shutil.which(command[0])
        if executable is None:
            pytest.skip(f"{command[0]} is not available")

        prepared = wrapped_command(
            ntrickle_paths,
            ntrickle_mode,
            [executable, *command[1:]],
            download_limit,
        )
        completed = subprocess.run(
            prepared.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout,
            cwd=prepared.cwd,
        )
        return CommandResult(
            stdout=completed.stdout,
            stderr=completed.stderr,
            returncode=completed.returncode,
        )

    return run


@pytest.fixture
def run_python_client(
    ntrickle_paths: NtricklePaths, ntrickle_mode: str
) -> Callable[..., CommandResult]:
    def run(
        code: str,
        url: str,
        *,
        timeout: float = 20.0,
        download_limit: str | None = None,
    ) -> CommandResult:
        prepared = wrapped_command(
            ntrickle_paths,
            ntrickle_mode,
            [sys.executable, "-c", code, url],
            download_limit,
        )
        completed = subprocess.run(
            prepared.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout,
            cwd=prepared.cwd,
        )
        return CommandResult(
            stdout=completed.stdout,
            stderr=completed.stderr,
            returncode=completed.returncode,
        )

    return run


def wait_for_unix_socket(path: Path, process: subprocess.Popen[bytes], timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None

    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            pytest.fail(
                "ntrickled exited before its socket became ready\n"
                f"stdout:\n{stdout.decode(errors='replace')}\n"
                f"stderr:\n{stderr.decode(errors='replace')}"
            )

        if path.exists():
            return

        time.sleep(0.05)

    pytest.fail(f"timed out waiting for ntrickled socket {path}: {last_error}")


@pytest.fixture
def ntrickled_daemon(tmp_path: Path, ntrickle_paths: NtricklePaths) -> Path:
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)

    socket_path = tmp_path / "ntrickled.sock"
    config_path = tmp_path / "trickled.conf"
    config_path.write_text("[General]\n", encoding="utf-8")

    process = subprocess.Popen(
        [
            str(ntrickle_paths.ntrickled_bin),
            "-n",
            str(socket_path),
            "-c",
            str(config_path),
            "-d",
            "1G",
            "-u",
            "1G",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        wait_for_unix_socket(socket_path, process)
        yield socket_path
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if socket_path.exists():
            socket_path.unlink()
