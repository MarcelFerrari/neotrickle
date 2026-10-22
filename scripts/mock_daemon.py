#!/usr/bin/env python3
"""
Mock ntrickle daemon control socket with configurable user count.

Usage::

    python3 scripts/mock_daemon.py [--users N] [socket_path]

The default socket is ``/tmp/ntrickled-mock.sock``.  The control socket for
the TUI is created at ``<socket_path>.ctl`` (matching the real daemon).

Press Ctrl-C to stop.
"""

import argparse
import json
import math
import os
import random
import signal
import socket
import sys
import time

# ---------------------------------------------------------------------------
# Global limits (configurable at top)
# ---------------------------------------------------------------------------
GLOBAL_DL_LIMIT = 10 * 1024 * 1024  # 10 MB/s
GLOBAL_UL_LIMIT = 5 * 1024 * 1024   # 5 MB/s

# ---------------------------------------------------------------------------
# User name pool
# ---------------------------------------------------------------------------
NAME_POOL = [
    "alice", "bob", "charlie", "diana", "eve", "frank", "grace",
    "henry", "iris", "jack", "kate", "leo", "mia", "noah", "olivia",
    "peter", "quinn", "rose", "sam", "tina", "ugo", "vera", "will",
    "xena", "yuri", "zane",
]

COMMAND_POOL = [
    "curl",
    "wget",
    "aria2c",
    "rsync",
    "python -m http.server",
    "pip install",
    "git clone",
    "scp",
    "aws s3 cp",
    "gcloud storage cp",
    "wget -r",
    "youtube-dl",
    "ffmpeg -i pipe:",
    "docker pull",
    "apt-get install",
    "npm install",
    "cargo build",
    "go mod download",
]


def make_user(uid: int, name: str, weight: int) -> dict:
    """Generate a random user with 1-4 sessions."""
    n_sessions = random.randint(1, 4)
    sessions = []
    for j in range(n_sessions):
        pid = 40000 + uid * 10 + j
        cmd = random.choice(COMMAND_POOL)
        # Random traffic profile
        dl_base = random.uniform(0.05e6, 5e6)   # 50 KB/s to 5 MB/s
        dl_amp = dl_base * random.uniform(0.3, 1.5)
        dl_phase = random.uniform(0, 6.28)
        dl_freq = random.uniform(0.05, 0.30)
        ul_base = random.uniform(0.01e6, 3e6)
        ul_amp = ul_base * random.uniform(0.2, 1.2)
        ul_phase = random.uniform(0, 6.28)
        ul_freq = random.uniform(0.05, 0.25)
        conn_offset = random.randint(10, 600)
        # Some sessions are idle
        idle_after = random.choice([None, None, None, None, None,  # mostly active
                                     random.randint(60, 300)])     # occasional idle

        sessions.append({
            "pid": pid,
            "argv0": cmd,
            "dl_base": dl_base, "dl_amp": dl_amp, "dl_phase": dl_phase, "dl_freq": dl_freq,
            "ul_base": ul_base, "ul_amp": ul_amp, "ul_phase": ul_phase, "ul_freq": ul_freq,
            "conn_offset": conn_offset,
            "idle_after": idle_after,
        })

    return {
        "name": name,
        "uid": uid,
        "weight": weight,
        "sessions": sessions,
    }


def compute_rate(base, amp, phase, freq, t):
    raw = base + amp * math.sin(t * freq + phase)
    noise = random.uniform(-0.10 * amp, 0.10 * amp)
    raw += noise
    return int(max(0, raw))


def generate_stats(user_defs: list, t: float) -> dict:
    users = []
    sessions = []
    total_weight = sum(u["weight"] for u in user_defs)

    for udef in user_defs:
        user_dl, user_ul = 0, 0
        user_bytes_dl, user_bytes_ul = 0, 0
        user_idle = True
        # Per-user cap (some users have explicit caps)
        dl_cap = 0
        ul_cap = 0
        if udef["name"] in ("charlie", "eve"):
            dl_cap = int(GLOBAL_DL_LIMIT * 0.3)
        if udef["name"] in ("bob", "diana"):
            ul_cap = int(GLOBAL_UL_LIMIT * 0.5)

        for sdef in udef["sessions"]:
            dl = compute_rate(sdef["dl_base"], sdef["dl_amp"],
                              sdef["dl_phase"], sdef["dl_freq"], t)
            ul = compute_rate(sdef["ul_base"], sdef["ul_amp"],
                              sdef["ul_phase"], sdef["ul_freq"], t)
            connected = int(t + sdef["conn_offset"])
            bytes_dl = int(sdef["dl_base"] * t +
                           sdef["dl_amp"] / max(sdef["dl_freq"], 0.01) *
                           (1 - math.cos(t * sdef["dl_freq"] + sdef["dl_phase"])))
            bytes_ul = int(sdef["ul_base"] * t +
                           sdef["ul_amp"] / max(sdef["ul_freq"], 0.01) *
                           (1 - math.cos(t * sdef["ul_freq"] + sdef["ul_phase"])))

            is_idle = False
            idle_after = sdef.get("idle_after")
            if idle_after is not None and t > idle_after:
                is_idle = True
                dl, ul = max(0, int(dl * 0.03)), max(0, int(ul * 0.03))

            if dl > 50_000 or ul > 50_000:
                user_idle = False

            # Per-session grant = proportional share of global limit
            session_grant_dl = int(GLOBAL_DL_LIMIT * udef["weight"] / total_weight / len(udef["sessions"]))
            session_grant_ul = int(GLOBAL_UL_LIMIT * udef["weight"] / total_weight / len(udef["sessions"]))

            sessions.append({
                "pid": sdef["pid"],
                "uid": udef["uid"],
                "argv0": sdef["argv0"],
                "grant_download_bps": session_grant_dl,
                "grant_upload_bps": session_grant_ul,
                "download_rate_bps": dl,
                "upload_rate_bps": ul,
                "bytes_downloaded": bytes_dl,
                "bytes_uploaded": bytes_ul,
                "connected_sec": connected,
                "is_idle": is_idle,
            })
            user_dl += dl
            user_ul += ul
            user_bytes_dl += bytes_dl
            user_bytes_ul += bytes_ul

        users.append({
            "uid": udef["uid"], "name": udef["name"],
            "weight": udef["weight"],
            "download_cap_bps": dl_cap, "upload_cap_bps": ul_cap,
            "download_rate_bps": user_dl, "upload_rate_bps": user_ul,
            "bytes_downloaded": user_bytes_dl, "bytes_uploaded": user_bytes_ul,
            "session_count": len(udef["sessions"]),
            "active_sessions": sum(1 for s in sessions
                                   if s["uid"] == udef["uid"] and not s["is_idle"]),
            "is_idle": user_idle,
        })

    # Apply a dynamic scaling envelope that varies over time instead of a fixed
    # 0.75 multiplier.  This creates natural peaks and troughs so the history
    # graphs are actually interesting to look at.
    # The envelope oscillates between ~0.35 (low) and ~1.15 (overshoot) with a
    # ~90 s period, plus a slower 5-minute drift, so utilization wanders across
    # the full gauge range.
    envelope = 0.65 + 0.35 * math.sin(t * 0.07) + 0.12 * math.sin(t * 0.021 + 2.3)

    # Scale all rates to stay within global limits (clamp overshoot)
    total_dl = sum(u["download_rate_bps"] for u in users)
    total_ul = sum(u["upload_rate_bps"] for u in users)
    dl_scale = 1.0 if total_dl <= GLOBAL_DL_LIMIT else GLOBAL_DL_LIMIT / max(total_dl, 1)
    ul_scale = 1.0 if total_ul <= GLOBAL_UL_LIMIT else GLOBAL_UL_LIMIT / max(total_ul, 1)
    dl_scale *= envelope
    ul_scale *= envelope * 0.9  # upload tends to lag a bit behind download

    for u in users:
        u["download_rate_bps"] = int(u["download_rate_bps"] * dl_scale)
        u["upload_rate_bps"] = int(u["upload_rate_bps"] * ul_scale)
        u["bytes_downloaded"] = int(u["bytes_downloaded"] * dl_scale)
        u["bytes_uploaded"] = int(u["bytes_uploaded"] * ul_scale)
    for s in sessions:
        s["download_rate_bps"] = int(s["download_rate_bps"] * dl_scale)
        s["upload_rate_bps"] = int(s["upload_rate_bps"] * ul_scale)
        s["bytes_downloaded"] = int(s["bytes_downloaded"] * dl_scale)
        s["bytes_uploaded"] = int(s["bytes_uploaded"] * ul_scale)

    users.sort(key=lambda u: u["download_rate_bps"] + u["upload_rate_bps"], reverse=True)
    sessions.sort(key=lambda s: s["download_rate_bps"] + s["upload_rate_bps"], reverse=True)

    return {
        "global": {
            "download_limit_bps": GLOBAL_DL_LIMIT,
            "upload_limit_bps": GLOBAL_UL_LIMIT,
            "fairness_mode": "dynamic",
            "idle_threshold_sec": 3,
            "burst_seconds": 0,
            "active_schedule": "",
            "uptime_sec": int(t),
        },
        "users": users,
        "sessions": sessions,
    }


def run_server(sock_path: str, n_users: int) -> None:
    ctl_path = sock_path + ".ctl"
    for p in (sock_path, ctl_path):
        if os.path.exists(p):
            os.unlink(p)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(ctl_path)
    server.listen(5)
    os.chmod(ctl_path, 0o666)

    # Generate users
    random.seed(42)  # reproducible patterns
    weights = [2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    user_defs = [make_user(1001 + i, NAME_POOL[i % len(NAME_POOL)],
                           weights[i % len(weights)])
                 for i in range(min(n_users, len(NAME_POOL)))]

    start_time = time.time()
    print(f"[mock] listening on {ctl_path}")
    print(f"[mock] {len(user_defs)} users, {sum(len(u['sessions']) for u in user_defs)} sessions")
    print(f"[mock] users: {', '.join(u['name'] for u in user_defs)}")
    print(f"[mock] connect with: trickle -n {sock_path} top --once")
    print(f"[mock] press Ctrl-C to stop")

    running = True
    def _handle(signum, frame):
        nonlocal running; running = False
    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    while running:
        try:
            server.settimeout(1.0)
            conn, _ = server.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        try:
            data = b""
            while True:
                chunk = conn.recv(4096)
                if not chunk: break
                data += chunk
                if b"\n" in data: break
            if not data: conn.close(); continue
            text = data.decode("utf-8", errors="replace").strip()
            try:
                req = json.loads(text)
            except json.JSONDecodeError:
                conn.sendall(json.dumps({"error": "invalid JSON"}).encode() + b"\n")
                conn.close(); continue
            cmd = req.get("cmd", "")
            elapsed = time.time() - start_time
            payload = generate_stats(user_defs, elapsed)
            if cmd == "stats":
                conn.sendall(json.dumps(payload).encode() + b"\n")
            elif cmd == "top":
                conn.sendall(json.dumps({"users": payload["users"]}).encode() + b"\n")
            else:
                conn.sendall(json.dumps({"error": f"unknown cmd: {cmd}"}).encode() + b"\n")
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            try: conn.close()
            except OSError: pass

    try: server.close()
    except OSError: pass
    for p in (sock_path, ctl_path):
        try: os.unlink(p)
        except OSError: pass
    print("[mock] stopped")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description="Mock ntrickle daemon")
    p.add_argument("--users", type=int, default=12, help="Number of users (default: 12)")
    p.add_argument("socket", nargs="?", default="/tmp/ntrickled-mock.sock",
                   help="Socket path (default: /tmp/ntrickled-mock.sock)")
    args = p.parse_args()
    run_server(args.socket, args.users)
