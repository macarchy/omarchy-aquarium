#!/bin/bash
# tests/test_toggle.sh — the toggle's bookkeeping: what it remembers across
# sessions, what "restore" does with it, and who it tells when the tank goes on
# or off. No compositor and no GPU: the renderer is a stub on PATH.
set -uo pipefail
cd "$(dirname "$0")/.."
TOGGLE="$PWD/bin/omarchy-aquarium-toggle"

TMP=$(mktemp -d)
cleanup() { pkill -f "$TMP/bin/omarchy-aquarium" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT

export HOME="$TMP/home" XDG_STATE_HOME="$TMP/state" XDG_RUNTIME_DIR="$TMP/run"
mkdir -p "$HOME/.config/omarchy-aquarium/hooks" "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR" "$TMP/bin"
export PATH="$TMP/bin:$PATH"
STATE="$XDG_STATE_HOME/omarchy-aquarium/enabled"
HOOKLOG="$TMP/hooks.log"

# A renderer that just sits there, and no notify-send: a test must not put
# bubbles on the screen of whoever is running it.
printf '#!/bin/bash\nsleep 5\n' > "$TMP/bin/omarchy-aquarium"
printf '#!/bin/bash\nexit 0\n'  > "$TMP/bin/notify-send"
printf '#!/bin/bash\nprintf "%%s\\n" "$1" >> "%s"\n' "$HOOKLOG" \
	> "$HOME/.config/omarchy-aquarium/hooks/record"
chmod +x "$TMP/bin/omarchy-aquarium" "$TMP/bin/notify-send" \
	"$HOME/.config/omarchy-aquarium/hooks/record"

fails=0
check() { local name=$1; shift; if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fails=$((fails+1)); fi; }
# Hooks are detached on purpose, so give them a moment to land.
hooks_say() {
	local want=$1 i
	for ((i = 0; i < 40; i++)); do
		[[ -f $HOOKLOG && $(tr '\n' ' ' <"$HOOKLOG") == "$want " ]] && return 0
		sleep 0.05
	done
	echo "    hooks were: $(tr '\n' ' ' <"$HOOKLOG" 2>/dev/null)" >&2
	return 1
}
state_is() { [[ $(cat "$STATE" 2>/dev/null) == "$1" ]]; }

"$TOGGLE" on >/dev/null
check "on remembers on"                 state_is on
check "on tells the hooks"              hooks_say "on"

: > "$HOOKLOG"
"$TOGGLE" off >/dev/null
check "off remembers off"               state_is off
check "off tells the hooks"             hooks_say "off"

: > "$HOOKLOG"
"$TOGGLE" restore >/dev/null
check "restore honours a deliberate off" state_is off
check "restore stays quiet when off"     [ ! -s "$HOOKLOG" ]

rm -f "$STATE"
"$TOGGLE" restore >/dev/null
check "restore with no state starts"     state_is on
check "restore tells the hooks"          hooks_say "on"

: > "$HOOKLOG"
"$TOGGLE" restart >/dev/null
check "restart keeps the state"          state_is on
check "restart is not a state change"    [ ! -s "$HOOKLOG" ]

"$TOGGLE" off >/dev/null
hooks_say "off"          # wait for it, or its detached hook lands mid-truncate
: > "$HOOKLOG"
"$TOGGLE" restart >/dev/null
check "restart on a stopped tank is a no-op" state_is off
check "  ... and says nothing"           [ ! -s "$HOOKLOG" ]

echo
(( fails )) && { echo "$fails failed"; exit 1; }
echo "all passed"
