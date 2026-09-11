# /etc/profile.d/zz99_utile_aliases.sh - Define useful aliases for Utile OS

# --- UTILE OS NALA CONFIG START --- #
apt() {
	HEADER="\e[31mUtile OS uses nala for this 'apt' command. To only use apt: modify your .bashrc, use 'sudo', or use 'apt-get'.\e[0m"
	if [[ "$1" == "history" || "$1" == "list" || "$1" == "search" || "$1" == "show" ]]; then
		echo -e "$HEADER"
		nala "$@"
	elif [[ "$1" == "autoclean" || "$1" == "reinstall" || "$1" == "satisfy" || "$1" == "source" || "$1" == "edit-sources" || "$2" == "--only-upgrade" || "$2" == "--reinstall" || "$2" == "--allow-downgrades" ]]; then
		sudo "apt" "$@"
	else
		echo -e "$HEADER"
		sudo nala "$@"
	fi
}
# --- UTILE OS NALA CONFIG END --- #

# Add new default neofetch aliases
alias fastfetch="fastfetch --logo /usr/share/pixmaps/utile-ascii.txt --config utile.jsonc"
alias neofetch="fastfetch"