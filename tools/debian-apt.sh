#!/bin/sh
# apt-get install in a Debian container that may be past its end of life
# (Debian 11, for the Linux packages: tools/build-linux.sh, packages.yml).
# Such a release moves from deb.debian.org to archive.debian.org, first its
# security updates, later all of it, while the image still names the old
# places. So: the image's sources; then deb.debian.org without the security
# updates; then archive.debian.org. Says which one worked.
#   tools/debian-apt.sh <package>...
set -u
export DEBIAN_FRONTEND=noninteractive
CODENAME=$(. /etc/os-release && echo "$VERSION_CODENAME")
try() {
  echo "debian-apt: $1"
  shift
  apt-get -o Acquire::Check-Valid-Until=false update -qq &&
    apt-get install -y -qq --no-install-recommends "$@" >/dev/null 2>/tmp/apt.err
}
sources() { rm -f /etc/apt/sources.list.d/*; printf '%s\n' "$@" > /etc/apt/sources.list; }
try "the image's sources" "$@" && exit 0
sources "deb http://deb.debian.org/debian $CODENAME main"
try "deb.debian.org, $CODENAME main only" "$@" && exit 0
sources "deb http://archive.debian.org/debian $CODENAME main" \
        "deb http://archive.debian.org/debian-security $CODENAME-security main"
try "archive.debian.org" "$@" && exit 0
cat /tmp/apt.err
echo "debian-apt: no source had the packages"
exit 1
