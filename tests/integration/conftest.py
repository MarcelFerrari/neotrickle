from __future__ import annotations

import hashlib
import json
import os
import shutil
import socket
import struct
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
DEFAULT_NTRICKLE_BIN = REPO_ROOT / "build" / "bin" / "trickle"
DEFAULT_NTRICKLED_BIN = REPO_ROOT / "build" / "bin" / "trickled"
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


@dataclass(frozen=True)
class DaemonHandle:
    socket_path: Path
    ctl_path: Path
    process: subprocess.Popen[bytes]


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
            stdout_text = stdout.decode(errors="replace") if stdout else ""
            stderr_text = stderr.decode(errors="replace") if stderr else ""
            pytest.fail(
                "ntrickled exited before its socket became ready\n"
                f"stdout:\n{stdout_text}\n"
                f"stderr:\n{stderr_text}"
            )

        if path.exists():
            return

        time.sleep(0.05)

    pytest.fail(f"timed out waiting for ntrickled socket {path}: {last_error}")


def query_ctl(ctl_path: Path, request: dict) -> dict:
    """Send one JSON request to the daemon control socket and parse the reply."""
    payload = (json.dumps(request) + "\n").encode("utf-8")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5.0)
        sock.connect(str(ctl_path))
        sock.sendall(payload)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return json.loads(b"".join(chunks).decode("utf-8"))


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("unexpected EOF from ntrickled")
        data.extend(chunk)
    return bytes(data)


def send_conf_and_read_grant(
    socket_path: Path,
    *,
    argv0: str = "probe",
    weight: int = 1,
    limits: tuple[int, int] = (0, 0),
    pid: int | None = None,
    uid: int | None = None,
    gid: int | None = None,
) -> tuple[int, int]:
    """Send a raw protocol-v3 CONF and return the initial (send, recv) grant."""
    payload = struct.pack(
        ">iiQQi",
        0,  # status
        1,  # MSG_TYPE_CONF
        limits[0],
        limits[1],
        os.getpid() if pid is None else pid,
    )
    payload += argv0.encode("utf-8")[:255].ljust(256, b"\0")
    payload += struct.pack(
        ">III",
        os.getuid() if uid is None else uid,
        os.getgid() if gid is None else gid,
        weight,
    )
    if len(payload) != 296:
        raise AssertionError(f"unexpected CONF payload size {len(payload)}")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5.0)
        sock.connect(str(socket_path))
        sock.sendall(b"\x03" + struct.pack(">I", len(payload)) + payload)

        version = _recv_exact(sock, 1)
        if version != b"\x03":
            raise AssertionError(f"unexpected protocol version {version!r}")
        (length,) = struct.unpack(">I", _recv_exact(sock, 4))
        body = _recv_exact(sock, length)

    status, msg_type = struct.unpack(">ii", body[:8])
    if msg_type != 9:  # MSG_TYPE_GRANT
        raise AssertionError(f"expected MSG_TYPE_GRANT, got {msg_type} (status={status})")
    send_bps, recv_bps = struct.unpack(">QQ", body[8:24])
    return send_bps, recv_bps


def _terminate_daemon(handle: DaemonHandle) -> None:
    if handle.process.poll() is None:
        handle.process.terminate()
        try:
            handle.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            handle.process.kill()
            handle.process.wait(timeout=5)
    for path in (
        handle.socket_path,
        handle.ctl_path,
        Path(str(handle.socket_path) + ".lock"),
    ):
        try:
            path.unlink()
        except FileNotFoundError:
            pass


@pytest.fixture
def ntrickled_daemon_factory(tmp_path: Path, ntrickle_paths: NtricklePaths):
    """Start isolated ntrickled instances for configuration tests."""
    require_ntrickle_paths(ntrickle_paths, include_daemon=True)
    handles: list[DaemonHandle] = []

    def start(
        *,
        config_text: str = "[General]\n",
        config_path: Path | None = None,
        extra_args: Sequence[str] = (),
        env: dict[str, str] | None = None,
        use_config_flag: bool = True,
    ) -> DaemonHandle:
        index = len(handles)
        socket_path = tmp_path / f"ntrickled-{index}.sock"
        if config_path is None:
            config_path = tmp_path / f"trickled-{index}.conf"
            config_path.write_text(config_text, encoding="utf-8")

        argv = [
            str(ntrickle_paths.ntrickled_bin),
            "-n",
            str(socket_path),
            "-d",
            "1G",
            "-u",
            "1G",
        ]
        if use_config_flag:
            argv.extend(["-c", str(config_path)])
        argv.extend(extra_args)

        process_env = os.environ.copy()
        for var in (
            "NTRICKLE_DAEMON_CONFIG",
            "NTRICKLE_PROCESS_CONFIG",
            "NTRICKLE_SCHEDULES_CONFIG",
        ):
            process_env.pop(var, None)
        if env is not None:
            process_env.update(env)

        process = subprocess.Popen(
            argv,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=process_env,
        )
        handle = DaemonHandle(
            socket_path=socket_path,
            ctl_path=Path(str(socket_path) + ".ctl"),
            process=process,
        )
        handles.append(handle)
        wait_for_unix_socket(socket_path, process)
        wait_for_unix_socket(handle.ctl_path, process)
        return handle

    try:
        yield start
    finally:
        for handle in reversed(handles):
            _terminate_daemon(handle)


@pytest.fixture
def ntrickled_daemon(ntrickled_daemon_factory) -> Path:
    return ntrickled_daemon_factory().socket_path
