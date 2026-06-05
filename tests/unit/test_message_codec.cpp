// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include <sys/types.h>

#include "message.h"
#include "message_codec.hpp"

#include <arpa/inet.h>
#include <vector>

extern "C" {
#include "codec.h"
}

static void test_conf_roundtrip() {
    struct msg m {};
    memset(&m, 0, sizeof(m));
    m.type = MSG_TYPE_CONF;
    m.data.conf.lim[0] = 100;
    m.data.conf.lim[1] = 200;
    m.data.conf.pid = 12345;
    strlcpy(m.data.conf.argv0, "/usr/bin/curl", sizeof(m.data.conf.argv0));
    m.data.conf.uid = 1000;
    m.data.conf.gid = 1000;
    m.data.conf.weight = 7;

    // Encode with C++ codec, decode with C XDR
    auto wire = ntrickled::MessageCodec::encode(m);
    // Strip version byte + 4-byte length prefix to get pure XDR payload
    if (wire.size() < 5) {
        std::fprintf(stderr, "FAIL conf: wire too short\n");
        std::exit(1);
    }
    uint32_t be_len;
    memcpy(&be_len, wire.data() + 1, 4);
    uint32_t payload_len = ntohl(be_len);
    if (1 + 4 + payload_len != wire.size()) {
        std::fprintf(stderr, "FAIL conf: length mismatch %zu vs %u\n", wire.size(),
                     1 + 4 + payload_len);
        std::exit(1);
    }

    struct msg decoded {};
    memset(&decoded, 0, sizeof(decoded));
    if (codec_decode(&decoded, const_cast<unsigned char*>(wire.data() + 5), payload_len) == -1) {
        std::fprintf(stderr, "FAIL conf: codec_decode failed\n");
        std::exit(1);
    }

    assert(decoded.type == MSG_TYPE_CONF);
    assert(decoded.data.conf.lim[0] == 100);
    assert(decoded.data.conf.lim[1] == 200);
    assert(decoded.data.conf.pid == 12345);
    assert(strcmp(decoded.data.conf.argv0, "/usr/bin/curl") == 0);
    assert(decoded.data.conf.uid == 1000);
    assert(decoded.data.conf.gid == 1000);
    assert(decoded.data.conf.weight == 7);
}

static void test_update_roundtrip() {
    struct msg m {};
    memset(&m, 0, sizeof(m));
    m.type = MSG_TYPE_UPDATE;
    m.data.update.len = 32768;
    m.data.update.dir = TRICKLE_RECV;

    auto wire = ntrickled::MessageCodec::encode(m);
    uint32_t be_len;
    memcpy(&be_len, wire.data() + 1, 4);
    uint32_t payload_len = ntohl(be_len);

    struct msg decoded {};
    memset(&decoded, 0, sizeof(decoded));
    assert(codec_decode(&decoded, const_cast<unsigned char*>(wire.data() + 5), payload_len) == 0);

    assert(decoded.type == MSG_TYPE_UPDATE);
    assert(decoded.data.update.len == 32768);
    assert(decoded.data.update.dir == TRICKLE_RECV);
}

static void test_grant_roundtrip() {
    struct msg m {};
    m.type = MSG_TYPE_GRANT;
    m.data.grant.grant[TRICKLE_SEND] = 1024000;
    m.data.grant.grant[TRICKLE_RECV] = 2048000;

    auto wire = ntrickled::MessageCodec::encode(m);
    uint32_t be_len;
    memcpy(&be_len, wire.data() + 1, 4);
    uint32_t payload_len = ntohl(be_len);

    struct msg decoded {};
    memset(&decoded, 0, sizeof(decoded));
    assert(codec_decode(&decoded, const_cast<unsigned char*>(wire.data() + 5), payload_len) == 0);

    assert(decoded.type == MSG_TYPE_GRANT);
    assert(decoded.data.grant.grant[TRICKLE_SEND] == 1024000);
    assert(decoded.data.grant.grant[TRICKLE_RECV] == 2048000);
}

static void test_c_xdr_to_cpp_codec() {
    // Encode a GRANT with C XDR, decode with C++ codec
    struct msg m {};
    m.type = MSG_TYPE_GRANT;
    m.data.grant.grant[TRICKLE_SEND] = 500000;
    m.data.grant.grant[TRICKLE_RECV] = 1000000;

    unsigned char buf[2048];
    uint32_t buflen = sizeof(buf);
    assert(codec_encode(&m, buf, &buflen) != -1);

    auto decoded = ntrickled::MessageCodec::decode(buf, buflen);
    assert(decoded.has_value());
    assert(decoded->type == MSG_TYPE_GRANT);
    assert(decoded->data.grant.grant[TRICKLE_SEND] == 500000);
    assert(decoded->data.grant.grant[TRICKLE_RECV] == 1000000);
}

int main() {
    test_conf_roundtrip();
    test_update_roundtrip();
    test_grant_roundtrip();
    test_c_xdr_to_cpp_codec();
    std::printf("PASS: all message_codec cross-tests\n");
    return 0;
}
