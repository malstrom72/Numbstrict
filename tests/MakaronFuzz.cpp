#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "../src/Makaron.h"
#include "FuzzInitialize.h"

/*
	Runs the input through `Makaron::Context::process` with `@include` served from the input itself, so the include
	path is fuzzed too: the input's sections, separated by zero bytes, are files named 1, 2, ... in order, and the first
	section is the source. Any other name, or a name past the last section, fails to load as a missing file does.
*/
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	std::vector<Makaron::String> files(1);
	for (size_t i = 0; i < size; ++i) {
		if (data[i] == 0) {
			files.push_back(Makaron::String());
		} else {
			files.back() += static_cast<char>(data[i]);
		}
	}
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
		Makaron::String processed;
		ctx.process(Makaron::Span(files[0], L"<fuzz>"), processed, nullptr);
	} catch (const Makaron::Exception&) {
	}
	return 0;
}
