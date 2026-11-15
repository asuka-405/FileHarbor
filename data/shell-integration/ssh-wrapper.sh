# `ssh` wrapper (bash and zsh). For a plain interactive login it starts the
# remote shell with the same OSC 7 integration, so the file manager can follow
# you around the remote machine. Anything else (commands, -N, -W, scp-style
# use, ...) is passed to the real ssh untouched.
# Disable with NEMO_NO_SSH_INTEGRATION=1.
ssh() {
	if [ -n "$NEMO_NO_SSH_INTEGRATION" ] || [ -z "$NEMO_INTEGRATION_DIR" ] || [ ! -t 0 ]; then
		command ssh "$@"
		return
	fi
	local skip=0 pend="" dest="" port="" user="" passthru=0 a opt
	for a in "$@"; do
		if [ "$skip" = 1 ]; then
			case "$pend" in
				p) port="$a" ;;
				l) user="$a" ;;
				o) case "$a" in
					[Uu]ser=*) user="${a#*=}" ;;
					[Pp]ort=*) port="${a#*=}" ;;
				   esac ;;
			esac
			skip=0
			continue
		fi
		case "$a" in
			-[bcDEeFIiJLlmOopQRSWw]) skip=1; pend="${a#-}"
				case "$pend" in O|Q|W) passthru=1 ;; esac ;;
			-p?*) port="${a#-p}" ;;
			-l?*) user="${a#-l}" ;;
			-o[Uu]ser=*) user="${a#-o*=}" ;;
			-o[Pp]ort=*) port="${a#-o*=}" ;;
			*=*) ;;
			-*[NTfGVn]*) passthru=1 ;;    # -N, -T, -f, -G, -V, -n (and clusters like -vN)
			-*) ;;
			*) if [ -z "$dest" ]; then dest="$a"; else passthru=1; fi ;;   # a remote command
		esac
	done
	if [ "$passthru" = 1 ] || [ -z "$dest" ]; then
		command ssh "$@"
		return
	fi
	dest="${dest#ssh://}"
	case "$dest" in
		*@*) user="${dest%@*}"; dest="${dest#*@}" ;;
	esac
	case "$dest" in
		*:*) port="${dest##*:}"; dest="${dest%:*}" ;;
	esac
	local target="${user:+$user@}$dest${port:+:$port}"
	local enc
	enc="$(awk -v t="$target" -v hook="$NEMO_INTEGRATION_DIR/integration.sh" '
		/^__HOOK__$/ { while ((getline l < hook) > 0) print l; next }
		{ gsub(/__TARGET__/, t); print }' "$NEMO_INTEGRATION_DIR/remote-bootstrap.sh.in" | base64 | tr -d '\n')"
	if [ -z "$enc" ]; then
		command ssh "$@"
		return
	fi
	command ssh -t "$@" "sh -c 'eval \"\$(echo $enc | base64 -d)\"'"
}
