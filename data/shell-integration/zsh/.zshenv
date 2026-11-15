# forwards to the user's own .zshenv
HISTFILE="${HISTFILE:-${NEMO_USER_ZDOTDIR:-$HOME}/.zsh_history}"
[ -f "${NEMO_USER_ZDOTDIR:-$HOME}/.zshenv" ] && source "${NEMO_USER_ZDOTDIR:-$HOME}/.zshenv"
