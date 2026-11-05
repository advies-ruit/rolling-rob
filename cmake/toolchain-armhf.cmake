# Cross-compile for 32-bit ARM Linux (Debian armhf: ARMv7-A, Thumb-2, VFPv3-D16
# hard float -- the same target as Debian's own armhf compiler) with Debian's
# crossbuild-essential-armhf, against the armhf libraries of a multiarch
# Debian (dpkg --add-architecture armhf). Used by tools/build-linux.sh.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
# programs (wayland-scanner, pkg-config) are the build machine's; libraries
# and headers come from /usr/lib/arm-linux-gnueabihf and /usr/include
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig")
