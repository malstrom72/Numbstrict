Numbstrict Documentation
=========================

Numbstrict is a minimal data format and parser for structured values.

Supported value types
---------------------

Numbstrict values can be parsed into these types:

- **Structs** – key/value pairs wrapped in `{}` with a colon after each key.
- **Arrays** – comma or newline separated values inside `{}`.
- **Text** – quoted with `"` or `'` or left unquoted when no special characters are present.
- **Numbers** – integers, unsigned integers and real numbers with optional exponent.
- **Booleans** – `true` and `false`.

Parsing rules and error handling
--------------------------------

- Commas and/or newlines separate list items in arrays and structs. Trailing commas are only allowed in arrays.
- A colon must follow struct keys on the same line. `{ : }` denotes an empty struct.
- Single line `//` and multi-line `/* */` comments may appear between values.
- Escape sequences in strings support `\n`, `\r`, `\t`, `\\`, `\x`, `\u` and `\U` forms.
- `Element::to<T>()` and `Parser::parse()` throw `ParsingError` when the source is invalid. `UndefinedElementError` is thrown when accessing a non-existent value.
- `Parser::tryToParse()` returns `false` on failure and leaves the read position at the point of error so the caller can recover or report diagnostics.

Examples
--------

### Element

```cpp
using namespace Numbstrict;

Element src("{ x: 1, nums: { 10, 20 } }");
Struct s = src.to<Struct>();
int x = s["x"].to<int>();
Array numbers = s["nums"].to<Array>();
```

### Parser

```cpp
using namespace Numbstrict;

Element src("{ x: 1, invalid }");
Parser p(src);
Struct s;
size_t offset;
if(!p.tryToParse(s, offset)) {
// handle error at offset
}
```

