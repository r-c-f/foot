PGO=auto           # auto|none|partial|full-current-session|full-headless-sway|full-headless-cage

pkgname=('foot-git' 'foot-terminfo-git')
pkgver=1.9.1
pkgrel=1
arch=('x86_64' 'aarch64')
url=https://codeberg.org/dnkl/foot
license=(mit)
depends=('libxkbcommon' 'wayland' 'pixman' 'fontconfig' 'libutf8proc' 'fcft>=2.4.0')
makedepends=('meson' 'ninja' 'scdoc' 'python' 'ncurses' 'wayland-protocols' 'tllist>=1.0.4')
source=()

pkgver() {
  cd ../.git &> /dev/null && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
      head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  ../pgo/pgo.sh ${PGO} .. . --prefix=/usr --wrap-mode=nofallback
}

check() {
  ninja test
}

package_foot-git() {
  pkgdesc="A wayland native terminal emulator"
  changelog=CHANGELOG.md
  depends+=('foot-terminfo')
  conflicts=('foot')
  provides=('foot')

  DESTDIR="${pkgdir}/" ninja install
  rm -rf "${pkgdir}/usr/share/terminfo"
}

package_foot-terminfo-git() {
  pkgdesc="Terminfo files for the foot terminal emulator"
  depends=('ncurses')
  conflicts=('foot-terminfo')
  provides=('foot-terminfo')

  install -dm 755 "${pkgdir}/usr/share/terminfo/f/"
  cp f/* "${pkgdir}/usr/share/terminfo/f/"
}
