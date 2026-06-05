# Limitations

`ntrickle` is Linux-only. It does not run on macOS, Windows, BSD, or any other platform.

`ntrickle` is a cooperative userspace tool. It is not a kernel enforcement system, a cgroups backend, a `tc` backend, or an anti-cheat security boundary.

Because throttling relies on `LD_PRELOAD`, it does not affect statically linked programs, privileged execution paths that ignore preload settings, programs that bypass libc wrappers, or traffic that is not visible to the interposed functions.

`ntrickle` does not replace an existing classic trickle installation. It uses its own runtime names and does not install legacy `trickle` aliases.
