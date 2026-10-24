// SPDX-FileCopyrightText: 2026 Marcel Ferrari
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon_context.hpp"

#include <cstring>
#include <sys/stat.h>

namespace ntrickled {

UnixListener::UnixListener(const char* path, mode_t mode) : path_(path) {
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ == -1)
        return;

    struct sockaddr_un xsun {};
    xsun.sun_family = AF_UNIX;
    std::strncpy(xsun.sun_path, path, sizeof(xsun.sun_path) - 1);
    xsun.sun_path[sizeof(xsun.sun_path) - 1] = '\0';

    // Set the umask so bind() creates the socket with the requested mode.
    // This avoids the race between bind() and chmod() where a client could
    // observe the socket with the wrong permissions.
    mode_t old_umask = umask(0777 & ~mode);
    if (bind(fd_, reinterpret_cast<struct sockaddr*>(&xsun), sizeof(xsun)) == -1) {
        umask(old_umask);
        close(fd_);
        fd_ = -1;
        return;
    }
    umask(old_umask);

    if (listen(fd_, 10) == -1) {
        close(fd_);
        fd_ = -1;
    }
}

void UnixListener::release() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
    if (!path_.empty()) {
        unlink(path_.c_str());
        path_.clear();
    }
}

UnixListener::~UnixListener() {
    release();
}

} // namespace ntrickled
