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

Real numbers
------------

- Syntax: an optional sign, digits with an optional fraction, and an optional exponent (`1.5`, `-2e-7`, `6.02E+23`),
  or `inf`, `-inf`, `+inf` and `nan` (`nan` takes no sign). `docs/numbstrict.peg` is the grammar.
- Parsing (`stringToDouble`, `stringToFloat`, `Element::to<double>()`, `Element::to<float>()`) returns the binary value
  nearest to the text, ties to even, whatever the number of digits; at the ends of the range that nearest value is zero
  or infinity.
- Formatting (`doubleToString`, `floatToString`, `compose`) writes the shortest text that reads back to the same value;
  when several texts of that length do, the closest, and on an exact half the one with an even last digit, as JavaScript
  prints it. Integers get `.0`, a decimal exponent below -6 or above 9 brings `e` notation (`1.5e-7`, `1.234e+20`),
  negative zero is `-0.0`, and `inf`, `-inf` and `nan` are written as such.
- Every finite double and float survives formatting and parsing unchanged, and neither direction depends on the
  floating-point environment (rounding mode, flush-to-zero, x87 precision). `docs/RealConversion.md` has the design
  and its verification.
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

