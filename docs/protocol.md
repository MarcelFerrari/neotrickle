# Protocol

The daemon and preload library communicate over a Unix domain socket using a grant-based protocol (v3).

## Wire format

Each message is framed as:

```
[1 byte: protocol version] [4 bytes: payload length, big-endian] [payload]
```

## Message types

| Type | Value | Direction | Purpose |
|---|---|---|---|
| `MSG_TYPE_CONF` | 1 | preload → daemon | Client registration: limits, PID, argv0, UID, GID, weight |
| `MSG_TYPE_GRANT` | 9 | daemon → preload | Rate grant: send/receive bytes per second |
| `MSG_TYPE_UPDATE` | 2 | preload → daemon | Fire-and-forget byte counts per direction |

### MSG_TYPE_CONF (288 bytes)

| Field | Offset | Size | Description |
|---|---|---|---|
| `lim[2]` | 0 | 16 | Self-reported send/recv limits (bytes/sec, advisory) |
| `pid` | 16 | 4 | Process ID |
| `argv0` | 20 | 256 | Command name (null-padded) |
| `uid` | 276 | 4 | User ID |
| `gid` | 280 | 4 | Group ID |
| `weight` | 284 | 4 | Bandwidth share weight (≥1) |

### MSG_TYPE_GRANT (16 bytes)

| Field | Offset | Size | Description |
|---|---|---|---|
| `grant[2]` | 0 | 16 | Send/recv rate limits (bytes/sec) |

### MSG_TYPE_UPDATE (16 bytes)

| Field | Offset | Size | Description |
|---|---|---|---|
| `len` | 0 | 8 | Byte count |
| `dir` | 8 | 2 | Direction (`TRICKLE_SEND` = 0, `TRICKLE_RECV` = 1) |

## Serialization

- **Preload (C)**: `codec.c`: standalone implementation using `htonl`/`memcpy`.
- **Daemon (C++)**: `message_codec.cpp`: standalone C++ implementation.
- **Verified by**: `test_message_codec`, which checks byte-for-byte roundtrip equivalence between the C and C++ codecs for all three message types.

## Version history

- **v1**: Original classic trickle delay-based protocol.
- **v2**: Grant-based protocol with 32-bit limits and `uint32_t` weight field (284-byte CONF).
- **v3**: 64-bit limits (`uint64_t lim[2]`), 288-byte CONF. Current protocol version.
