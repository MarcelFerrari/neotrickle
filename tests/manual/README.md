# Manual test HTTP server

Generates arbitrary amounts of data on-the-fly (no disk usage). Useful for
ad-hoc manual testing of bandwidth throttling with curl, wget, or custom
clients.

This is a standalone tool for interactive use. Automated tests use the
embedded HTTP fixture in `tests/integration/conftest.py` instead.

## Usage

```bash
python3 tests/manual/server.py [port]   # default port 8080
```

## Endpoints

| Path | Description |
|---|---|
| `/<size>` | Download `<size>` bytes of data, e.g. `/1G`, `/500M`, `/10M`, `/1M` |
| `/?size=500M` | Same via query parameter |
| `/health` | Health check (tiny response) |

Size suffixes: `K` (KiB), `M` (MiB), `G` (GiB) — powers of 1024.

## Examples

```bash
# Without trickle
curl -o /dev/null http://127.0.0.1:8080/1G
wget -O /dev/null http://127.0.0.1:8080/500M

# With trickle (daemon must be running)
trickle -- curl -o /dev/null http://127.0.0.1:8080/100M
trickle -d 50M -- wget -O /dev/null http://127.0.0.1:8080/1G
```
