# FIX and TODO Summary

## src/Numbstrict.cpp

### FIX: quoteString should accept ranges (line 270)
`quoteString` currently takes a `std::basic_string`, forcing callers such as `compose(const Char*)` to allocate a temporary `String`. Accepting a pair of iterators or a pointer range would allow quoting arbitrary buffers without extra allocation.

### TODO: stringToInt hexadecimal coverage (lines 1686–1690)
The unit test block has commented-out assertions for hexadecimal values `0x89abcdef`, `0x7fffffff`, and `0xffffffff`. Parsing fails for the larger values, showing `stringToInt` cannot handle the full `uint32_t` range. Support for numbers beyond `INT_MAX` and matching tests are needed.

## src/Makaron.h

### FIX: include file names in RangeVector (line 135)
`RangeVector` tracks `[begin, end)` offsets but omits the originating file. When macros or includes span multiple files, error reporting loses file context. Each range should also record the file name.

## src/Makaron.cpp

### TODO: optimize findInputRanges search (line 685)
`findInputRanges` currently performs a linear scan through `offsetMap`. Replacing this with a binary search would improve performance on large maps.

## src/Numbstrict.h

### TODO: implement charsToInt (line 354)
A low-level `charsToInt` function is mentioned in comments to parse integers directly from character arrays. This routine is missing and would enable parsing without creating temporary `String` objects.

### TODO: implement hexCharsToInt (line 356)
Similarly, a hexadecimal version `hexCharsToInt` is declared but unimplemented, leaving no efficient way to parse hex digits from character ranges.
