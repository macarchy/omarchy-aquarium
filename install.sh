#!/bin/bash
# omarchy-aquarium install — build the renderer, install it, and wire it into
# Omarchy: the SUPER+ALT+A toggle, the login "restore", and the theme hook.
#
# `make install` only ever placed three binaries in ~/.local/bin; everything
# that makes the aquarium feel installed — the keybind above all — was wired by
# a separate, Apple-Silicon-only bootstrap, so anyone else got a renderer with
# no way to reach it (issue #3).
#
# Idempotent: every step converges or is skipped with a note.
#
#   ./install.sh              build, install, wire
#   ./install.sh --uninstall  stop it, remove the binaries, hook and wiring

set -uo pipefail
cd "$(dirname "$0")"

BIN="${BIN:-$HOME/.local/bin}"
HOOK_DIR="$HOME/.config/omarchy/hooks/theme-set.d"
HYPR="$HOME/.config/hypr"
MARK="omarchy-aquarium"

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }
warn() { printf '    \033[33mwarning: %s\033[0m\n' "$*" >&2; FAILURES=$((FAILURES+1)); }
FAILURES=0

# Which file takes the wiring: Omarchy 4 configures Hyprland in Lua, earlier
# versions in hyprlang. Wire whichever this machine actually has.
bindings_file() {
	[[ -f $HYPR/bindings.lua ]] && { echo "$HYPR/bindings.lua"; return; }
	[[ -f $HYPR/bindings.conf ]] && { echo "$HYPR/bindings.conf"; return; }
	[[ -f $HYPR/hyprland.conf ]] && { echo "$HYPR/hyprland.conf"; return; }
	echo ""
}
autostart_file() {
	[[ -f $HYPR/autostart.lua ]] && { echo "$HYPR/autostart.lua"; return; }
	bindings_file          # hyprlang has no separate autostart file
}

if [[ ${1:-} == --uninstall ]]; then
	command -v omarchy-aquarium-toggle >/dev/null && omarchy-aquarium-toggle off
	rm -f "$BIN"/omarchy-aquarium "$BIN"/omarchy-aquarium-toggle \
	      "$BIN"/omarchy-aquarium-notify "$HOOK_DIR/aquarium-theme"
	for f in $(bindings_file) $(autostart_file); do
		[[ -n $f && -f $f ]] || continue
		python3 - "$f" "$MARK" <<'PY'
import sys
path, mark = sys.argv[1], sys.argv[2]
lines = open(path).read().splitlines(keepends=True)
keep, dropping = [], False
for line in lines:
    if f">>> {mark}" in line:
        dropping = True
        continue
    if dropping:
        if f"<<< {mark}" in line:
            dropping = False
        continue
    keep.append(line)
open(path, "w").write("".join(keep))
PY
		note "$(basename "$f"): wiring removed"
	done
	echo "omarchy-aquarium removed. Your ~/.local/state/omarchy-aquarium state file was kept."
	exit 0
fi

# ---------------------------------------------------------------- build

say "Checking build dependencies"
missing=()
for p in wayland-client wayland-egl egl glesv2; do
	pkg-config --exists "$p" || missing+=("$p")
done
command -v wayland-scanner >/dev/null || missing+=("wayland-scanner")
if ((${#missing[@]})); then
	warn "missing: ${missing[*]}"
	note "Arch:   sudo pacman -S --needed wayland wayland-protocols mesa"
	note "Debian: sudo apt install libwayland-dev libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev wayland-protocols"
	exit 1
fi
note "all present"

say "Building and installing into $BIN"
make -s BIN="$BIN" install || { warn "build failed"; exit 1; }
note "renderer, toggle and notification watcher installed"
case ":$PATH:" in
	*":$BIN:"*) ;;
	*) warn "$BIN is not on your PATH; the keybind will not find the toggle" ;;
esac

# ----------------------------------------------------------------- hook

say "Installing the theme-set hook"
if [[ -d $HOME/.config/omarchy ]]; then
	mkdir -p "$HOOK_DIR"
	install -m755 hooks/aquarium-theme "$HOOK_DIR/aquarium-theme"
	note "$HOOK_DIR/aquarium-theme"
else
	note "no ~/.config/omarchy: skipping the theme hook (not an Omarchy machine?)"
fi

# -------------------------------------------------------------- wiring

# Guarded append, fenced by markers so --uninstall can take it back out again.
append_once() {   # append_once <file> <guard> <<'EOF' ... EOF
	local file=$1 guard=$2 body pfx
	body=$(cat)
	# The fence is a comment in the file's own language: '#' is not a comment
	# in Lua, and a '#' marker would take the whole config down with it.
	[[ $file == *.lua ]] && pfx='--' || pfx='#'
	if [[ -z $file ]]; then
		warn "no Hyprland config found in $HYPR; wire this by hand:"
		printf '%s\n' "$body" | sed 's/^/        /'
		return
	fi
	if grep -qF "$guard" "$file"; then
		note "$(basename "$file"): already wired ($guard)"
		return
	fi
	{ printf '\n%s >>> %s\n' "$pfx" "$MARK"; printf '%s\n' "$body"; printf '%s <<< %s\n' "$pfx" "$MARK"; } >>"$file"
	note "$(basename "$file"): wired $guard"
}

say "Wiring Hyprland"
BINDF=$(bindings_file)
AUTOF=$(autostart_file)

if [[ $BINDF == *.lua ]]; then
	append_once "$BINDF" "omarchy-aquarium-toggle" <<'LUA'
-- Animated underwater scene on the layer-shell "bottom" layer: above the
-- wallpaper, below every window. (github.com/macarchy/omarchy-aquarium)
o.bind("SUPER + ALT + A", "Aquarium background", "omarchy-aquarium-toggle")
LUA
else
	append_once "$BINDF" "omarchy-aquarium-toggle" <<'CONF'
# Animated underwater scene on the layer-shell "bottom" layer.
# (github.com/macarchy/omarchy-aquarium)
bindd = SUPER ALT, A, Aquarium background, exec, omarchy-aquarium-toggle
CONF
fi

if [[ $AUTOF == *.lua ]]; then
	append_once "$AUTOF" "omarchy-aquarium-toggle restore" <<'LUA'
-- The aquarium, put back the way it was left: "restore" starts it unless it
-- was deliberately toggled off in an earlier session.
o.exec_on_start("omarchy-aquarium-toggle restore")
LUA
else
	append_once "$AUTOF" "omarchy-aquarium-toggle restore" <<'CONF'
# The aquarium, put back the way it was left.
exec-once = omarchy-aquarium-toggle restore
CONF
fi

# ------------------------------------------------------------ live bits

if [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]]; then
	say "Applying it to the running session"
	hyprctl reload >/dev/null 2>&1 && note "hyprctl reload (SUPER+ALT+A is live)"
	"$BIN"/omarchy-aquarium-toggle restore && note "aquarium restored to its remembered state"
else
	note "no Hyprland session: it starts at your next login"
fi

if (( FAILURES )); then
	printf '\n\033[33mFinished with %d warning(s).\033[0m\n' "$FAILURES"
	exit 1
fi
printf '\n\033[1;32momarchy-aquarium installed.\033[0m SUPER + ALT + A toggles it.\n'
