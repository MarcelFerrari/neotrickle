#!/usr/bin/env python3
"""
Test HTTP server for bandwidth-limiter testing.

Serves arbitrary amounts of data generated on-the-fly (no disk usage).
Supports both curl and wget.  Run from the ntrickle build directory:

    python3 tests/manual/server.py              # default: port 8080

Endpoints:
    /<size>          e.g. /1G, /500M, /10M, /1M
    /?size=500M      query-parameter form
    /health          health-check (tiny response)

Size suffixes:  K (KiB), M (MiB), G (GiB) — powers of 1024.
"""
import http.server
import sys
import re
import os

_SIZE_RE = re.compile(r"^/?(\d+)([KMGkmg])?$", re.ASCII)

_UNITS = {"K": 1024, "M": 1024**2, "G": 1024**3,
          "k": 1024, "m": 1024**2, "g": 1024**3}

CHUNK = 1024 * 1024  # 1 MiB per write


def parse_size(s: str) -> int:
    """Parse a size string like '1G', '500M', '10M' into bytes."""
    m = _SIZE_RE.match(s)
    if not m:
        raise ValueError(f"invalid size: {s!r}")
    value = int(m.group(1))
    unit = m.group(2)
    if unit:
        value *= _UNITS[unit]
    return value


class Handler(http.server.BaseHTTPRequestHandler):

    def do_GET(self):
        # Parse size from path or query string
        size_str = None
        path = self.path.split("?", 1)[0]

        if path == "/health":
            self._text(200, "ok")
            return

        # /<size> form
        stripped = path.rstrip("/")
        if stripped and stripped != "/":
            size_str = stripped.lstrip("/")

        # ?size=... form
        if size_str is None:
            qs = self.path.split("?", 1)[1] if "?" in self.path else ""
            for pair in qs.split("&"):
                if "=" in pair:
                    k, v = pair.split("=", 1)
                    if k == "size":
                        size_str = v
                        break

        if size_str is None:
            self._text(400, "Usage: /<size>  e.g. /1G, /500M, /10M\n"
                            "       /?size=500M")
            return

        try:
            total = parse_size(size_str)
        except ValueError:
            self._text(400, f"Invalid size: {size_str!r}\n"
                            "Usage: /<size>  e.g. /1G, /500M, /10M")
            return

        if total <= 0:
            self._text(400, "Size must be > 0")
            return

        self._stream(total)

    def _text(self, code: int, body: str):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _stream(self, total: int):
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(total))
        self.end_headers()

        left = total
        chunk = bytearray(CHUNK)
        # Fill with repeating pattern so it's non-zero data
        mv = memoryview(chunk)
        for i in range(0, CHUNK, 256):
            mv[i:i+256] = bytes(range(256))

        try:
            while left > 0:
                n = min(CHUNK, left)
                if n == CHUNK:
                    self.wfile.write(chunk)
                else:
                    self.wfile.write(mv[:n])
                left -= n
        except (BrokenPipeError, ConnectionResetError):
            pass  # client disconnected

    def log_message(self, fmt, *args):
        # Quieter logging
        sys.stderr.write(f"[ntrickle-test-server] {args[0]}\n")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    addr = ("127.0.0.1", port)
    server = http.server.HTTPServer(addr, Handler)
    print(f"ntrickle test server listening on http://{addr[0]}:{addr[1]}")
    print("Endpoints: /<size>  e.g. /1G  /500M  /10M  /1M  /health")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
        server.shutdown()


if __name__ == "__main__":
    main()
