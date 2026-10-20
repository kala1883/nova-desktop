# libssh2 1.11.1

C sources and public headers from https://libssh2.org/download/libssh2-1.11.1.tar.gz
(released 2024-10-16). Archive SHA256:
`d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7`.
Public `include/libssh2.h` SHA256:
`86cc9fcfd0daa10ba25442e5ecea23db618027362bc85fdc591ec7f5d9d39a91`.

Local formatting adjustment: removed one extra blank line at the end of
`src/chacha.h` to pass the repository's Git whitespace checks. No functional
changes were made to the upstream sources.

License: BSD-3-Clause, see COPYING and the notices retained in individual files.
`deployment/build-ssh.bat` builds a static relocatable object with the OpenSSL
backend. MinGW-w64 OpenSSL 3 development headers and libcrypto.a are build
dependencies; libcrypto is linked statically. The server needs SSH, SFTP and a
POSIX shell, with no NOVA agent. Disabled legacy algorithms are explicit in the
build script. Dependency updates must update this provenance and run SSH tests.

The build resolves OpenSSL from the selected MinGW prefix, its `opt` directory,
or `NOVA_OPENSSL_ROOT`. It adds the matching include/library paths to the build
process and uses `-nostdlib -r` for the relocatable object, excluding executable
startup code. The object cache tracks the compiler, OpenSSL prefix and headers.
