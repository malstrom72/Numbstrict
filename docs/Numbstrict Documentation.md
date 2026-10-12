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

- Syntax: an optional sign, digits with an optional point and fraction, and an optional exponent (`1.5`, `-2e-7`,
  `6.02E+23`), or `inf`, `-inf`, `+inf` and `nan` (`nan` takes no sign). As other formatters write them, a point may
  lack a digit on one side (`.5`, `5.`), the integer part may have leading zeros (`007.5`) and the exponent may be
  padded (`1e-005`). Digits alone with a leading zero (`007`) are neither an integer nor a real, since C reads them
  as octal; a `Variant` reads them as text. `docs/numbstrict.peg` is the grammar.
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

Conversion functions
--------------------

- `stringToInt`, `stringToInt64`, `stringToUnsignedInt`, `stringToUnsignedInt64`, `stringToFloat` and `stringToDouble`
  convert text in the manner of `strtol` and `strtod`, and the `charsTo` versions do the same on a pointer range
  without allocating: leading and trailing white space (not comments), an optional sign (a minus only for the signed
  functions), then as many characters as make a number, leading zeros included, so `"12.5"` gives `12` from
  `stringToInt` and `"007"` gives `7`.
- `nextOffset` (or `next`) is where the scan stopped: the length of the text when it took everything, and the start
  when nothing was read, as `strtol`'s end pointer. An integer past the range of its type is the nearest limit of that
  type, with `nextOffset` at the first digit that does not fit, so comparing `nextOffset` with the length detects it;
  a real past its range is infinity or zero. Integers take hexadecimal after `0x`; a bare `0x` is the number 0
  followed by the letter x.
- `hexStringToUnsignedInt`, `hexStringToUnsignedInt64` and their `hexCharsTo` versions scan the same way but read
  bare hex digits of either case, with no sign and no prefix, the inverse of `unsignedIntToHexString`,
  `unsignedInt64ToHexString` and their `Chars` forms: `"ff"` gives `255`, and `"0x1f"` gives `0` with the scan
  stopped at the x.
- The formatters mirror the parsers, named after the type they take: `intToString`, `int64ToString`,
  `unsignedIntToString`, `unsignedInt64ToString`, `unsignedIntToHexString` and `unsignedInt64ToHexString`, and the
  same six with `Chars`, which write into the caller's buffer with a terminating zero and do not allocate.
- `compose`, `quoteString` and `quoteWideString` also take a character range [`begin`, `end`), so text that is not
  in a `String` is quoted without being copied into one first.
- `Parser::tryToParse` and `Element::to` read a whole value by the grammar instead: white space and comments around
  it, no leading zeros, and failure for anything out of range or left over.

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

Command-line tool
-----------------

`NumbstrictCmd [-n] <input file>|- [<output file>|-]`, built by `build.sh` and `build.cmd` into `output/<target>/`,
checks that the input is valid Numbstrict, parsing every nested value, and exits with 1 and the error on stderr when
it is not. With `-n` it also writes the input normalized: comments dropped, a struct or array on one line when that
fits in 40 columns and otherwise one entry per line indented with tabs, keys in the order of the input and unquoted
when they are identifiers, quoted text in double quotes and unquoted text as it was, integers in decimal unless the
input wrote them in hexadecimal, reals in their shortest form, and lines ending with a line feed whatever the
platform. Normalizing twice gives the same text, so the tool suits pre-commit hooks and CI. `tests/normalize.numbstrict`
and `tests/normalize.expected.numbstrict` show the form.
