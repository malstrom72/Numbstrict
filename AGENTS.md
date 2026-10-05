# Repository Guidelines

To run the test suite use the helper script, allowing up to three minutes for execution:

```bash
bash build.sh
```

On Windows run `build.cmd` instead. Where the `timeout` command exists (Linux), `timeout 180 bash build.sh` enforces the
limit; macOS has no `timeout`.

Always execute this command before committing changes to verify that the build and regression tests succeed.

## Repository layout
The project uses a consistent folder structure. Build output is written to `output/` and no source files live there. Useful locations:

- `tools/`: scripts for building and maintaining the code and documentation.
- `projects/`: Xcode and Visual Studio project files.
- `docs/`: documentation.
- `externals/`: projects and source code from other repositories (only touch this content when explicitly asked to).
- `src/`: C++ source code for the library. The library is distributed as source rather than prebuilt binaries.
- `tests/`: regression tests.
- `examples/`: small sample programs.
- `benchmarks/`: JavaScript performance tests.
- `output/`: contains only build artifacts (and any runtime dependencies), no source files.

Root-level `build.sh` and `build.cmd` (mirrored implementations) should build and test both the beta and release targets.
These builds must compile the sources using the C++11 standard (`-std=c++11` or `/std:c++14` on MSVC).

### PikaCmd directory
The `externals/PikaCmd` folder is a separate project copied into this repository. Ignore it when applying formatting or running tests.

### BuildCpp
BuildCpp.sh and BuildCpp.cmd are copied from another repository. Only make changes to them if there is no other solution.

## Coding style
Code style (design rules, naming, comments, formatting, Markdown and commit messages) is defined in
`docs/CodingStyle.md`. That file is shared with other projects: change nothing above its "Local additions" section,
and put Numbstrict-only style rules in that section or here. Parts of `src/` predate the rules: the existing
`/** **/` and `///` comments are legacy, so do not copy them, and do not convert a file wholesale in an unrelated
change.

### Command-line tools
When handling files with command-line tools (which may break tab characters):
- Always run `expand -t 4` on the file before processing.
- Always run `unexpand -t 4` on the file after processing.

## Fuzzing
- `tools/build_numbstrict_fuzz.*` and `tools/build_makaron_fuzz.*` build the libFuzzer targets, optimized with asserts
  on: MSVC with AddressSanitizer on Windows, clang with AddressSanitizer and UBSan elsewhere (Homebrew LLVM on macOS).
  Each script's comment shows how to run its target.
- `tests/fuzz/` holds each target's minimized corpus (`numbstrictCorpus.tar.gz`, `makaronCorpus.tar.gz`) and
  dictionary. `build.sh` and `build.cmd` unpack the corpora into `output/fuzzReplay/` and replay them through the
  targets in beta builds, using `tests/FuzzMain.cpp` instead of libFuzzer.
- Check the machine's load before a long run. On macOS run under `nohup caffeinate -i ... & disown`, with
  `ASAN_OPTIONS=detect_container_overflow=0:external_symbolizer_path=$(brew --prefix llvm)/bin/llvm-symbolizer` and
  `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`.
- Refresh a corpus only after a substantial run: merge into an empty directory with `-merge=1`, then pack it
  deterministically from `output/`, e.g.
  `tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2000-01-01 00:00Z' -cf - numbstrict | gzip -9n`.
  Add the input behind any fixed fuzzer finding to the corpus, so the replay keeps checking it.

## Script portability
All user-facing `.sh` and `.cmd` files must work when launched from any directory. They should start by changing to their own folder (or the repository root) so that relative paths resolve correctly.

`.sh` scripts must be runnable without requiring `chmod +x`; always invoke them with `bash path/to/script.sh` (do
**not** rely on the system-default `sh`).  Each script must start with a portable she-bang:

```
#!/usr/bin/env bash
set -e -o pipefail -u
```

Every `.sh` script must have a corresponding `.cmd` implementation with identical behavior. Use `.cmd` files rather than `.bat`.

```
# example for a shell script
cd "$(dirname "$0")"/..
```

REM example for a .cmd script  
```
CD /D "%~dp0\.."
```

For robust error handling, `.sh` scripts should begin as shown above, and `.cmd` scripts normally use a simple error check:

```
CALL buildAndTest.cmd %target% || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
```
