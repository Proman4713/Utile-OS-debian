# /etc/profile.d/zz99_utile_xdg_dirs.sh - Prepend /usr/share/utile to $XDG_DATA_DIRS

XDG_DATA_DIRS="/usr/share/utile:${XDG_DATA_DIRS#*/usr/share/utile:}"
export XDG_DATA_DIRS
