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


