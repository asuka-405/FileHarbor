# Terminal integration for the file manager (bash and zsh).
# Reports the working directory after every prompt using the standard OSC 7
# sequence, so the file view can follow `cd`. Over ssh, NEMO_T carries the
# remote target (user@host:port) so the view can follow into sftp://.
__nemo_urlenc() {
	local LC_ALL=C s="$1" i c out=""
	for (( i = 0; i < ${#s}; i++ )); do
		c="${s:$i:1}"
		case "$c" in
			[a-zA-Z0-9/._~-]) out+="$c" ;;
			*) out+="$(printf '%%%02X' "'$c")" ;;
		esac
	done
	printf '%s' "$out"
}

__nemo_osc7() {
	printf '\033]7;file://%s%s\033\\' \
		"$(__nemo_urlenc "${NEMO_T:-${HOST:-$HOSTNAME}}")" "$(__nemo_urlenc "$PWD")"
}

if [ -n "$ZSH_VERSION" ]; then
	autoload -Uz add-zsh-hook 2>/dev/null && add-zsh-hook precmd __nemo_osc7 || precmd_functions+=(__nemo_osc7)
elif [ -n "$BASH_VERSION" ]; then
	case ";$PROMPT_COMMAND;" in
		*";__nemo_osc7;"*) ;;
		*) PROMPT_COMMAND="__nemo_osc7${PROMPT_COMMAND:+;$PROMPT_COMMAND}" ;;
	esac
fi
