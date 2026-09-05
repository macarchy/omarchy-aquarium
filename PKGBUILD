# Maintainer: Philippe Matray <phmatray@gmail.com>
#
# The one repo in this org that actually compiles: nine C sources behind a
# Makefile. So this is arch=('aarch64') and NOT arch=('any') like its siblings,
# and it cannot be built in the x86_64 container the others use.
# macarchy-install#18.
pkgname=omarchy-aquarium
# Rewritten from the tag by the packaging job before makepkg runs. This value is
# the fallback for a manual makepkg from a checkout.
pkgver=0.2.1
pkgrel=1
pkgdesc="A live aquarium as your desktop background: one GLSL shader on a Wayland layer surface"
# Built and verified on real hardware, never cross-compiled and never under
# emulation -- either produces a package nobody has run.
arch=('aarch64')
url="https://github.com/macarchy/omarchy-aquarium"
license=('MIT')
install=omarchy-aquarium.install
depends=('wayland' 'mesa' 'libglvnd')
makedepends=('wayland-protocols' 'pkgconf')
optdepends=('omarchy: the theme hook and the notify watcher target it'
            'hyprland: the layer surface this draws on')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "$srcdir/$pkgname-$pkgver"
  make
}

package() {
  cd "$srcdir/$pkgname-$pkgver"

  # The Makefile's install rule takes PREFIX and has no DESTDIR, so the staging
  # root goes through PREFIX. Reusing the repo's own rule is what keeps the two
  # install channels from drifting -- there is no second list of files here.
  make install PREFIX="$pkgdir/usr"

  # The theme hook belongs to omarchy's hook directory under $HOME, which pacman
  # may not write. It ships as a template and the scriptlet says where.
  install -Dm755 hooks/aquarium-theme "$pkgdir/usr/share/$pkgname/hooks/aquarium-theme"

  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
}
