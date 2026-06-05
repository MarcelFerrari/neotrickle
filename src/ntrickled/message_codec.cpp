// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "message_codec.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "message.h"

#include <arpa/inet.h>

namespace {

int write_all(int fd, const void* data, size_t len) {
    size_t written = 0;
    auto* p = static_cast<const char*>(data);
    while (written < len) {
        ssize_t n = write(fd, p + written, len - written);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        written += static_cast<size_t>(n);
    }
    return 0;
}

int read_all(int fd, void* buf, size_t len) {
    size_t nread = 0;
    auto* p = static_cast<char*>(buf);
    while (nread < len) {
        ssize_t n = read(fd, p + nread, len - nread);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        nread += static_cast<size_t>(n);
    }
    return 0;
}

} // namespace

namespace ntrickled {

// ---------------------------------------------------------------------------
// Wire-format helpers
// ---------------------------------------------------------------------------

namespace {

void write_u32(std::vector<uint8_t>& out, uint32_t val) {
    uint32_t be = htonl(val);
    auto* p = reinterpret_cast<const uint8_t*>(&be);
    out.insert(out.end(), p, p + sizeof(be));
}

void write_u64(std::vector<uint8_t>& out, uint64_t val) {
    // XDR hyper: big-endian 64-bit — high 4 bytes first, then low 4
    uint32_t hi = htonl(static_cast<uint32_t>(val >> 32));
    uint32_t lo = htonl(static_cast<uint32_t>(val & 0xFFFFFFFFULL));
    auto* phi = reinterpret_cast<const uint8_t*>(&hi);
    auto* plo = reinterpret_cast<const uint8_t*>(&lo);
    out.insert(out.end(), phi, phi + 4);
    out.insert(out.end(), plo, plo + 4);
}

void write_i32(std::vector<uint8_t>& out, int32_t val) {
    uint32_t be = htonl(static_cast<uint32_t>(val));
    auto* p = reinterpret_cast<const uint8_t*>(&be);
    out.insert(out.end(), p, p + sizeof(be));
}

void write_short(std::vector<uint8_t>& out, short val) {
    write_i32(out, static_cast<int32_t>(val));
}

void write_opaque(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

bool read_u32(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    if (p + 4 > end)
        return false;
    uint32_t be;
    memcpy(&be, p, 4);
    out = ntohl(be);
    p += 4;
    return true;
}

bool read_u64(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    if (p + 8 > end)
        return false;
    uint32_t hi, lo;
    memcpy(&hi, p, 4);
    memcpy(&lo, p + 4, 4);
    out = (static_cast<uint64_t>(ntohl(hi)) << 32) | ntohl(lo);
    p += 8;
    return true;
}

bool read_i32(const uint8_t*& p, const uint8_t* end, int32_t& out) {
    uint32_t raw;
    if (!read_u32(p, end, raw))
        return false;
    out = static_cast<int32_t>(raw);
    return true;
}

bool read_short(const uint8_t*& p, const uint8_t* end, short& out) {
    int32_t val;
    if (!read_i32(p, end, val))
        return false;
    out = static_cast<short>(val);
    return true;
}

bool read_opaque(const uint8_t*& p, const uint8_t* end, uint8_t* dst, size_t len) {
    if (p + len > end)
        return false;
    memcpy(dst, p, len);
    p += len;
    return true;
}

void encode_header(std::vector<uint8_t>& out, short status, int type) {
    write_short(out, status);
    write_i32(out, type);
}

bool decode_header(const uint8_t*& p, const uint8_t* end, short& status, int& type) {
    return read_short(p, end, status) && read_i32(p, end, type);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MessageCodec
// ---------------------------------------------------------------------------

std::vector<uint8_t> MessageCodec::encode(const struct msg& m) {
    std::vector<uint8_t> out;
    out.reserve(512);

    encode_header(out, m.status, static_cast<int>(m.type));

    switch (m.type) {
    case MSG_TYPE_CONF: {
        const auto& c = m.data.conf;
        write_u64(out, c.lim[0]);
        write_u64(out, c.lim[1]);
        write_i32(out, c.pid);
        write_opaque(out, reinterpret_cast<const uint8_t*>(c.argv0), sizeof(c.argv0));
        write_u32(out, static_cast<uint32_t>(c.uid));
        write_u32(out, static_cast<uint32_t>(c.gid));
        write_u32(out, c.weight);
        break;
    }
    case MSG_TYPE_GRANT: {
        const auto& g = m.data.grant;
        write_u64(out, g.grant[TRICKLE_SEND]);
        write_u64(out, g.grant[TRICKLE_RECV]);
        break;
    }
    case MSG_TYPE_UPDATE: {
        const auto& u = m.data.update;
        write_u32(out, static_cast<uint32_t>(u.len));
        write_short(out, u.dir);
        break;
    }
    }

    // Framing: [1 byte version] [4 bytes len BE] [payload]
    std::vector<uint8_t> framed;
    framed.reserve(5 + out.size());
    framed.push_back(PROTOCOL_VERSION);
    write_u32(framed, static_cast<uint32_t>(out.size()));
    framed.insert(framed.end(), out.begin(), out.end());
    return framed;
}

std::optional<struct msg> MessageCodec::decode(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 8)
        return std::nullopt;

    const uint8_t* p = data;
    const uint8_t* end = data + len;

    short status = 0;
    int type_val = 0;
    if (!decode_header(p, end, status, type_val))
        return std::nullopt;

    struct msg m {};
    memset(&m, 0, sizeof(m));
    m.status = status;
    m.type = static_cast<enum msgtype>(type_val);

    switch (m.type) {
    case MSG_TYPE_CONF: {
        auto& c = m.data.conf;
        if (!read_u64(p, end, c.lim[0]))
            return std::nullopt;
        if (!read_u64(p, end, c.lim[1]))
            return std::nullopt;
        if (!read_i32(p, end, reinterpret_cast<int32_t&>(c.pid)))
            return std::nullopt;
        if (!read_opaque(p, end, reinterpret_cast<uint8_t*>(c.argv0), sizeof(c.argv0)))
            return std::nullopt;
        if (!read_u32(p, end, reinterpret_cast<uint32_t&>(c.uid)))
            return std::nullopt;
        if (!read_u32(p, end, reinterpret_cast<uint32_t&>(c.gid)))
            return std::nullopt;
        if (!read_u32(p, end, c.weight))
            return std::nullopt;
        break;
    }
    case MSG_TYPE_GRANT: {
        auto& g = m.data.grant;
        if (!read_u64(p, end, g.grant[TRICKLE_SEND]))
            return std::nullopt;
        if (!read_u64(p, end, g.grant[TRICKLE_RECV]))
            return std::nullopt;
        break;
    }
    case MSG_TYPE_UPDATE: {
        auto& u = m.data.update;
        uint32_t raw_len;
        if (!read_u32(p, end, raw_len))
            return std::nullopt;
        u.len = static_cast<size_t>(raw_len);
        if (!read_short(p, end, u.dir))
            return std::nullopt;
        break;
    }
    default:
        return std::nullopt;
    }

    if (p != end)
        return std::nullopt;
    return m;
}

std::optional<struct msg> MessageCodec::read_from(int fd) {
    uint8_t version;
    if (read_all(fd, &version, 1) == -1)
        return std::nullopt;
    if (version != PROTOCOL_VERSION)
        return std::nullopt;

    uint32_t be_len;
    if (read_all(fd, &be_len, sizeof(be_len)) == -1)
        return std::nullopt;
    uint32_t payload_len = ntohl(be_len);

    if (payload_len > PROTOCOL_MAX_MSG_SIZE)
        return std::nullopt;

    std::vector<uint8_t> payload(payload_len);
    if (read_all(fd, payload.data(), payload_len) == -1)
        return std::nullopt;

    return decode(payload.data(), payload_len);
}

bool MessageCodec::write_to(int fd, const struct msg& m) {
    auto framed = encode(m);
    return write_all(fd, framed.data(), framed.size()) == 0;
}

} // namespace ntrickled
