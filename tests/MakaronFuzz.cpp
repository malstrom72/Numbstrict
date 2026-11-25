#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include "../src/Makaron.h"
#include "FuzzInitialize.h"

/*
	Runs the input through `Makaron::Context::process` with `@include` served from the input itself, so the include
	path is fuzzed too: the input's sections, separated by zero bytes, are files named 1, 2, ... in order, and the first
	section is the source. Any other name, or a name past the last section, fails to load as a missing file does.

	An input of one section then runs again with @ and % swapped and % as the prefix. Neither character means anything
	else to the parser, so the second run must give the first run's output with @ and % swapped, and fail at the same
	place. Included files are always processed with @, which breaks the symmetry, hence the single section.
*/

struct Outcome {
	Makaron::String output;																								// as far as it got, when the run throws
	bool threw;
	std::wstring errorFile;
	size_t errorOffset;
};

static Outcome run(const std::vector<Makaron::String>& files, Makaron::Char prefix) {
	Outcome outcome;
	outcome.threw = false;
	outcome.errorOffset = 0;
	try {
		Makaron::Context ctx;
		ctx.setIncludeLoader([&files](const Makaron::WideString& name, Makaron::String& contents) {
			size_t index = 0;
			for (Makaron::WideString::const_iterator p = name.begin(); p != name.end(); ++p) {
				if (*p < '0' || *p > '9' || index > files.size()) {
					return false;
				}
				index = index * 10 + (*p - '0');
			}
			if (name.empty() || index == 0 || index >= files.size()) {
				return false;
			}
			contents = files[index];
			return true;
		});
		ctx.process(Makaron::Span(files[0], L"<fuzz>"), outcome.output, nullptr, prefix);
	} catch (const Makaron::Exception& exception) {
		outcome.threw = true;
		outcome.errorFile = exception.getFile();
		outcome.errorOffset = exception.getOffset();
	}
	return outcome;
}

static Makaron::String swapAtAndPercent(Makaron::String s) {
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '@') {
			s[i] = '%';
		} else if (s[i] == '%') {
			s[i] = '@';
		}
	}
	return s;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	std::vector<Makaron::String> files(1);
	for (size_t i = 0; i < size; ++i) {
		if (data[i] == 0) {
			files.push_back(Makaron::String());
		} else {
			files.back() += static_cast<char>(data[i]);
		}
	}
	const Outcome at = run(files, '@');
	if (files.size() != 1) {
		return 0;
	}
	files[0] = swapAtAndPercent(files[0]);
	const Outcome percent = run(files, '%');
	if (percent.threw != at.threw || percent.errorFile != at.errorFile || percent.errorOffset != at.errorOffset
			|| percent.output != swapAtAndPercent(at.output)) {
		abort();
	}
	return 0;
}
