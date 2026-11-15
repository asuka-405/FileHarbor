# forwards to the user's own .zlogin
[ -f "${NEMO_USER_ZDOTDIR:-$HOME}/.zlogin" ] && source "${NEMO_USER_ZDOTDIR:-$HOME}/.zlogin"
