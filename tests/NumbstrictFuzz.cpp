#include <cstddef>
#include <cstdint>
#include <string>
#include "../src/Numbstrict.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	const std::string input(reinterpret_cast<const char*>(data), size);
	try {
		Numbstrict::Struct s = Numbstrict::parseStruct(input, "fuzz");
		Numbstrict::compose(s);
	} catch (const Numbstrict::Exception&) {
	}
	return 0;
}
