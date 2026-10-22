# Limitations

`ntrickle` is Linux-only. It does not run on macOS, Windows, BSD, or any other platform.

`ntrickle` is a cooperative userspace tool. It is not a kernel enforcement system, a cgroups backend, a `tc` backend, or an anti-cheat security boundary.

Because throttling relies on `LD_PRELOAD`, it does not affect statically linked programs, privileged execution paths that ignore preload settings, programs that bypass libc wrappers, or traffic that is not visible to the interposed functions.

Interposition covers TCP sockets created with `AF_INET` or `AF_INET6`. UDP and non-IP sockets are not limited.

The Debian package can replace the classic `trickle` package, and the installed command names are `trickle`/`trickled`. The command-line interface, configuration format, and wire protocol are modern and are not drop-in compatible with classic trickle; see [compatibility.md](compatibility.md) and [migration-classic.md](migration-classic.md).
