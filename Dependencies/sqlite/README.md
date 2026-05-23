# SQLite (vendored amalgamation)

Vendored copy of the upstream SQLite amalgamation. We compile `sqlite3.c`
straight into `TONE3000Player.vst3` so there is **no runtime DLL
dependency** on the host's `sqlite3.dll` (some DAWs ship a stale one, and
Windows has no system-wide SQLite).

## Source

- **Version:** SQLite 3.46.0
- **Upstream:** <https://sqlite.org/2024/sqlite-amalgamation-3460000.zip>
- **SHA-256 of the .zip:** `712a7d09d2a22652fb06a49af516e051979a3984adb067da86760e60ed51a7f5`

Only `sqlite3.c` and `sqlite3.h` were extracted; the upstream `shell.c`
and `sqlite3ext.h` files were not vendored (we don't ship the CLI shell
and we don't load runtime extensions).

## Build integration

`sqlite3.c` is plain C — the Visual Studio project entry sets
`<PrecompiledHeader>NotUsing</PrecompiledHeader>` on its `<ClCompile>`
node so MSVC's PCH machinery (which assumes C++) doesn't try to apply
the project-wide `iplug2_common.h` precompiled header to it.

The include path `..\Dependencies\sqlite` is appended to the project's
`AdditionalIncludeDirectories` in `NeuralAmpModeler-win.props`, so
consumers can write `#include "sqlite3.h"`.

## Refreshing this vendored copy

```
cd nam-fork/Dependencies/sqlite
curl -L -o sqlite-amalgamation.zip \
  https://sqlite.org/<YEAR>/sqlite-amalgamation-<VERSION>.zip
unzip -q sqlite-amalgamation.zip
mv sqlite-amalgamation-*/sqlite3.c .
mv sqlite-amalgamation-*/sqlite3.h .
rm -rf sqlite-amalgamation-* sqlite-amalgamation.zip
# Update the version + SHA-256 above.
```

`.gitignore` keeps `sqlite-amalgamation*` (zip + unpacked folder) out of
the tree in case the refresh leaves residue.
