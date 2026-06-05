// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include <optional>
#include <vector>

struct msg; // from message.h (C header)

namespace ntrickled {

class MessageCodec {
public:
    // Encode a struct msg to wire format bytes.
    // Wire format: [1 byte version] [4 bytes len BE] [XDR payload].
    static std::vector<uint8_t> encode(const struct msg& m);

    // Decode wire format bytes to a struct msg.
    // Returns std::nullopt if the data is malformed or truncated.
    static std::optional<struct msg> decode(const uint8_t* data, size_t len);

    // Read one message from a socket fd.
    // Returns std::nullopt on error, disconnect, or corrupt data.
    static std::optional<struct msg> read_from(int fd);

    // Write one message to a socket fd.
    // Returns true on success, false on error.
    static bool write_to(int fd, const struct msg& m);

    // Max wire size for any message (for buffer sizing).
    static constexpr size_t max_wire_size() {
        return 2048;
    }
};

} // namespace ntrickled
