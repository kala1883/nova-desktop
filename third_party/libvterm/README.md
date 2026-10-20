# libvterm 0.3.3

Unmodified C sources and public headers from Paul Evans' upstream release:
https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz

Archive SHA256:
`09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`

Only `src/`, `include/` and `LICENSE` are included. `SHA256SUMS` records the
exact vendored file hashes. The library is compiled as C11 and statically linked
by `deployment/build-terminal.bat`; it requires no extra DLL or process and
retains Windows 7 compatibility. NOVA's Win32 terminal renderer lives in
`src/ui/terminal_view.c`, separate from the unmodified library.

The upstream `vterm_keycodes.h`, `parser.c` and `unicode.c` contain trailing
spaces. Root `.gitattributes` disables only `blank-at-eol` checks for these three
files, preserving their original bytes and hashes. Other whitespace checks and
all first-party checks remain enabled, including deployment's staged-diff check.

License: MIT, see `LICENSE`. `src/unicode.c` also retains Markus Kuhn's permissive
wcwidth notice. Both notices are included in the distributed
`THIRD_PARTY_NOTICES.txt`.

Compatibility coverage in `tests/terminal.c` exercises UTF-8/CJK, SGR, cursor
addressing, alternate screens, key encoding, device replies, paste and bounded
scrollback. `tests/ssh_terminal.c` uses the loopback SSH fixture to exercise the
real native view, worker, resize and lifecycle. Run `deployment/test-ssh.bat`
and then `python tests/ssh_fixture.py build/ssh-client-test.exe` (Paramiko required).
