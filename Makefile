PACKAGE := pve-journalreader

ARCH != dpkg-architecture -qDEB_BUILD_ARCH
PKGVER != dpkg-parsechangelog -S version
GITVERSION:=$(shell git rev-parse HEAD)

all: $(DEB)

DEB=${PACKAGE}_${PKGVER}_${ARCH}.deb

.PHONY: deb
deb: $(DEB)
$(DEB):
	rm -rf build
	rsync -a ./src/* build/
	rsync -a ./debian build/
	echo "git clone git://git.proxmox.com/git/pve-journalreader.git\\ngit checkout $(GITVERSION)" > build/debian/SOURCE
	cd build; dpkg-buildpackage -b -us -uc
	lintian $(DEB)

.PHONY: clean
clean:
	rm -rf build/ *.deb *.buildinfo *.changes
