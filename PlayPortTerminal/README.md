# PortShell — PlayPort terminal prototype

PortShell is a small native Windows console executable intended to be tested inside PlayPort/Wine. It is a **prototype**, not a complete POSIX shell or a guaranteed OpenCode/ADB runtime.

## Included in v0.1

- Interactive console prompt
- Built-ins: `help`, `pwd`, `cd`, `dir`/`ls`, `mkdir`, `rmdir`, `del`/`rm`, `copy`/`cp`, `type`/`cat`, `where`, `cls`, `exit`
- Direct external-program launch via Win32 `CreateProcessW`
- Initial working directory override through `PORTSHELL_ROOT`
- UTF-8 console code page setup

## Important PlayPort limitations to test

1. **Shared files:** PortShell can access only paths visible to the Wine process. It does not magically bypass the iOS sandbox or expose LiveContainer/Files folders. Set `PORTSHELL_ROOT` to a path that PlayPort already maps and exposes.
2. **Child processes:** External commands depend on PlayPort's Wine/FEX process implementation. If `CreateProcessW` is unsupported or incomplete for a program, that program will not start.
3. **ADB:** This build does not bundle Android Platform Tools. Add a compatible Windows `adb.exe` and its required DLLs to a directory on PATH. ADB's background server (normally TCP port 5037) must be tested separately in PlayPort.
4. **OpenCode:** OpenCode is not bundled. Its Windows runtime, Node/Bun dependencies, network access, terminal capabilities, and child-process behavior all need validation. Full-screen TUI support is not guaranteed by this basic console.
5. **Architecture:** Start with x64 Windows binaries only if that matches the PlayPort guest/runtime configuration. A Windows ARM64 build may be needed for a different setup.

## Build locally (Visual Studio 2022)

Open a Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.`buildReleasePortShell.exe
```

## Build on GitHub Actions

A workflow in `.github/workflows/portshell-build.yml` builds the x64 Windows executable and uploads it as the artifact **PortShell-Windows-x64**. Open the repository's Actions tab, select the latest successful run, and download the artifact ZIP.

The executable is not committed to the source tree; it is produced by the Windows CI runner. This avoids checking an unverified binary into the repository.
