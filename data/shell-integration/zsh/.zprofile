# forwards to the user's own .zprofile
[ -f "${NEMO_USER_ZDOTDIR:-$HOME}/.zprofile" ] && source "${NEMO_USER_ZDOTDIR:-$HOME}/.zprofile"
