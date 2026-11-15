# Used by the file manager's terminal: loads the user's own .zshrc, then adds
# the integration, then hands ZDOTDIR back to the user.
HISTFILE="${HISTFILE:-${NEMO_USER_ZDOTDIR:-$HOME}/.zsh_history}"
[ -f "${NEMO_USER_ZDOTDIR:-$HOME}/.zshrc" ] && source "${NEMO_USER_ZDOTDIR:-$HOME}/.zshrc"
source "$NEMO_INTEGRATION_DIR/integration.sh"
source "$NEMO_INTEGRATION_DIR/ssh-wrapper.sh"
ZDOTDIR="${NEMO_USER_ZDOTDIR:-$HOME}"
