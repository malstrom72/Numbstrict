# Repository Guidelines

To run the test suite use the helper script with up to three minutes allowed for execution:

```bash
timeout 180 ./build.sh
```

Always execute this command before committing changes to verify that the build and regression tests succeed.

## Repository layout
The project uses a consistent folder structure. Build output is written to `output/` and no source files live there. Useful locations:

- `tools/` - scripts for building and maintaining the code and documentation.
- `projects/` - Xcode and Visual Studio project files.
- `docs/` - documentation.
- `externals/` - projects and source code from other repositories (only touch this content when explicitly asked to).
- `src/` - C++ source code for the library. The library is distributed as source rather than prebuilt binaries.
- `tests/` - regression tests.
- `examples/` - small sample programs.
- `benchmarks/` - JavaScript performance tests.
- `output/` - contains only build artifacts (and any runtime dependencies), no source files.

Root-level `build.sh` and `build.cmd` (mirrored implementations) should build and test both the beta and release targets.
These builds must compile the sources using the C++11 standard (`-std=c++11` or `/std:c++14` on MSVC).

### PikaCmd directory
The `externals/PikaCmd` folder is a separate project copied into this repository. Ignore it when applying formatting or running tests.

### BuildCpp
BuildCpp.sh and BuildCpp.cmd are copied from another repository. Only make changes to them if there is no other solution.

## Coding style
These rules apply to new and edited code. Parts of `src/` predate them: do not copy the older style, and do not convert
a file wholesale in an unrelated change.

### Design
- A constructor either produces a fully valid object or throws (RAII). No two-phase construction, no `init()` or
  `isValid()` to check afterwards.
- Assert liberally for programmer errors, preferably as `assert(condition && "why this must hold")`. Include it as
  `#include "assert.h"` (quotes) so a project can override the handler. Never use `abort()`.
- Validate untrusted input once, where it enters (the parsers). Inside that boundary, trust the contract: no repeated
  defensive checks and no fallback branches for states that cannot happen.
- Throw for runtime conditions (malformed input, allocation failure), never for programmer errors. Never return a
  half-filled result or a success code from a path that failed.
- No duplicated functionality. Generalize the existing function instead of adding a near-copy, and prefer one code path
  over special cases: fewer paths is the most important thing for correctness. A refactor should make the code smaller.
- Optimize only for a measured win that matters.
- Use full words in names (`functionCount`, not `fnCount`). Boolean queries are named `isX()`.
- Data members are private. Use grouped `public:` / `protected:` / `private:` sections, public first. Big function
  bodies go in the `.cpp`, and internal helpers stay out of the public header.
- In a `.cpp`, file-scope helpers are `static`, never `static inline`. Headers keep `inline` where it is needed.

### Formatting
- Tab characters for indentation, *not spaces*. A tab character equals four spaces.
- Opening braces stay on the same line as the control statement and closing braces are on their own line.
- `if`, `else`, `for`, `while`, `do` and `switch` always use braces, with the body on its own lines, even for a single
  statement.
- A short function body of one or two simple statements may sit on one line: `int size() const { return count; }`.
- One declaration per line.
- Maximum line width is 120 characters.
- Line continuations should start with the operator and be indented two tabs from the original line.
- `#if`/`#endif` blocks should appear one tab *left* of the current indentation level.
- Plain ASCII only: no en or em dashes (U+2013, U+2014), curly quotes or other lookalikes, in code, comments, docs or
  commit messages.

### Comments
- Comment sparingly: the non-obvious why, an invariant or a gotcha, never what the code already says.
- Short comments are a single end-of-line `//`, starting at column 120 (padded with tabs). A run of related
  declarations may align to a common column instead.
- Longer comments are a `/* */` block with the body indented one tab, not a stack of `//` lines:
	```
	/*
		One or more sentences.
	*/
	```
- No Doxygen: no `///`, `///<`, `/** */` or `@param`. The existing `/** **/` and `///` comments are legacy.
- Inside comment text, wrap any variable, parameter, class or function names in back-ticks, e.g. `blah` is the
  temporary buffer.

### Markdown
- Pad table cells so the pipes line up, within the 120-column limit. ASCII diagrams must actually align.

### Command-line tools
When handling files with command-line tools (which may break tab characters):
- Always run `expand -t 4` on the file before processing.
- Always run `unexpand -t 4` on the file after processing.

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
