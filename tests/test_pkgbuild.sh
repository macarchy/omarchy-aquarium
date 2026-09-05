#!/bin/bash
# tests/test_pkgbuild.sh — the one repo in the org that actually compiles.
#
# Its siblings are arch=('any') and build in an x86_64 container. This one has
# nine C sources behind a Makefile, so it is arch=('aarch64') and must be built
# ON aarch64 — never cross-compiled, never under qemu, both of which produce a
# package nobody has run. macarchy-install#18.
set -uo pipefail
cd "$(dirname "$0")/.."

fails=0
check() { local name=$1; shift; if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fails=$((fails+1)); fi; }
# Comments name every artefact, so a whole-file grep passes even when the line is
# gone. Read the code.
code() { grep -v '^[[:space:]]*#' PKGBUILD; }
WF=.github/workflows/release-please.yml
# Same for the workflow: its comments say "no qemu", so a whole-file grep for
# qemu matches the very sentence promising there is none. Read the code.
wf_code() { grep -v '^[[:space:]]*#' "$WF"; }

check "the package is aarch64, not any"     grep -q "arch=('aarch64')" <(code)
check "it builds through the Makefile"      grep -qx '  make' <(code)
check "and installs through the repo's own rule" \
  grep -q 'make install PREFIX="\$pkgdir/usr"' <(code)
check "wayland/EGL are makedepends"         grep -q 'makedepends=.*wayland-protocols' <(code)
check "the theme hook ships as a template"  grep -q 'hooks/aquarium-theme' <(code)
check "and the scriptlet says where"        grep -q 'theme-set.d' omarchy-aquarium.install

# The runner and image have to be aarch64 too, and the result is ASSERTED, not
# assumed: a package whose .PKGINFO says otherwise was built somewhere it should
# not have been.
check "built on an aarch64 runner"          grep -q 'runs-on: ubuntu-24.04-arm' "$WF"
check "in an Arch ARM image"                grep -q 'container: menci/archlinuxarm' "$WF"
check "no qemu emulation step"              bash -c '! grep -v "^[[:space:]]*#" "$1" | grep -qi qemu' _ "$WF"
check "the built arch is verified"          grep -q "grep -qx 'arch = aarch64'" "$WF"

# The workflow lessons from macarchy-install#16, each a real failure there.
check "no standalone package workflow"      [ ! -e .github/workflows/package.yml ]
check "the job hangs off release_created"   grep -q 'release_created' "$WF"
check "not a release: published trigger"    bash -c '! grep -q "types: \[published\]" '"$WF"
check "pkgver is rewritten from the tag"    grep -q 'pkgver=\${TAG#v}' "$WF"
check "extra-files is not used"             bash -c '! grep -q "extra-files" release-please-config.json'
check "the upload globs"                    grep -q '\*.pkg.tar.\*' "$WF"
check "gh is installed in the container"    grep -q 'github-cli' "$WF"

(( fails == 0 )) && echo "all ok" || echo "$fails failed"
exit $(( fails > 0 ))
