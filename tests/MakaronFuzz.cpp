#include <cstddef>
#include <cstdint>
#include <string>
#include "../src/Makaron.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	try {
		Makaron::String src(reinterpret_cast<const char*>(data), size);
		Makaron::Context ctx;
		Makaron::String processed;
		ctx.process(Makaron::Span(src, L"<fuzz>"), processed, nullptr);
	} catch (const Makaron::Exception&) {
	}
	return 0;
}
