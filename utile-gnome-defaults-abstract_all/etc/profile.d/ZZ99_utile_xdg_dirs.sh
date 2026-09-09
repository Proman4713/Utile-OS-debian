# /etc/profile.d/ZZ99_utile_xdg_dirs.sh - Prepend /usr/share/utile to $XDG_CONFIG_DIRS and $XDG_DATA_DIRS

export XDG_DATA_DIRS="/usr/share/utile:$XDG_DATA_DIRS"