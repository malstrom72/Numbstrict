# Numbstrict

## Project Summary

Numbstrict is a C++ library for parsing and composing a strict text-based data format. It can read and write arrays, structures and primitive values with predictable formatting.

## Build

Use the provided scripts to compile the library and run the regression tests.

```bash
./build.sh
```

On Windows:

```cmd
build.cmd
```

## Fuzzing

The repository includes libFuzzer harnesses for the Numbstrict and Makaron parsers. The helper scripts compile the fuzz targets with address and fuzzer sanitizers.

### Numbstrict

```bash
bash tools/build_numbstrict_fuzz.sh
```

The resulting binary is placed in `output/NumbstrictFuzz` and can be run with a directory containing seed inputs:

```bash
./output/NumbstrictFuzz corpus/
```

### Makaron

```bash
bash tools/build_makaron_fuzz.sh
```

The resulting binary is placed in `output/MakaronFuzz` and accepts a corpus directory like the Numbstrict target.

On macOS the default clang from Xcode does not ship the libFuzzer runtime. Install the `llvm` package via Homebrew and invoke the scripts with that compiler:

```bash
CPP_COMPILER=$(brew --prefix llvm)/bin/clang++ bash tools/build_numbstrict_fuzz.sh
```

## Usage

```cpp
#include "Numbstrict.h"

int main() {
	Numbstrict::Struct data = Numbstrict::parseStruct("{ foo: 42 }", "example");
	std::string encoded = Numbstrict::compose(data);
	return 0;
}
```

See the [documentation](docs/) for more details and advanced examples.

## License

This project is licensed under the [BSD 2-Clause License](LICENSE).
