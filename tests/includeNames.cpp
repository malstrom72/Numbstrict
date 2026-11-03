/*
	The default include loader opens files with non-ASCII names: on Windows a name written in UTF-8 or in ISO 8859-1,
	elsewhere the name's bytes as they are, here UTF-8. Creates its files in the directory given as the argument.
*/
#include <cstdio>
#include <exception>
#include "../src/Makaron.h"
#include "QuietCrt.h"
#if defined(_WIN32) || defined(_WIN64)
	#include <direct.h>
#else
	#include <unistd.h>
#endif

struct Case {
	const char* source;
	const char* expected;
};

static bool createFile(const char* utf8Name, const wchar_t* wideName, const char* contents) {
#if defined(_WIN32) || defined(_WIN64)
	(void)utf8Name;
	FILE* const file = _wfopen(wideName, L"wb");
#else
	(void)wideName;
	FILE* const file = fopen(utf8Name, "wb");
#endif
	if (file == 0) {
		return false;
	}
	const bool written = (fputs(contents, file) >= 0);
	return (fclose(file) == 0 && written);
}

int main(int argc, const char* argv[]) {
	quietCrt();
	if (argc != 2) {
		fprintf(stderr, "Usage: includeNames <directory for its files>\n");
		return 1;
	}
#if defined(_WIN32) || defined(_WIN64)
	const bool changed = (_chdir(argv[1]) == 0);
#else
	const bool changed = (chdir(argv[1]) == 0);
#endif
	if (!changed) {
		fprintf(stderr, "Cannot change to %s\n", argv[1]);
		return 1;
	}
	if (!createFile("caf\xC3\xA9.makaron", L"caf\xE9.makaron", "accent\n")
			|| !createFile("\xF0\x9F\x90\x99.makaron", L"\xD83D\xDC19.makaron", "octopus\n")) {
		fprintf(stderr, "Cannot create the include files\n");
		return 1;
	}
	const Case CASES[] = {
		{ "@include caf\xC3\xA9.makaron\n", "accent\n" },
		{ "@include \xF0\x9F\x90\x99.makaron\n", "octopus\n" },
#if defined(_WIN32) || defined(_WIN64)
		{ "@include caf\xE9.makaron\n", "accent\n" },
#endif
	};
	int failures = 0;
	for (size_t i = 0; i < sizeof (CASES) / sizeof (*CASES); ++i) {
		try {
			const Makaron::String output = Makaron::process(CASES[i].source, L"main.makaron");
			if (output != CASES[i].expected) {
				fprintf(stderr, "Case %d gave \"%s\"\n", static_cast<int>(i), output.c_str());
				++failures;
			}
		}
		catch (const std::exception& exception) {
			fprintf(stderr, "Case %d threw: %s\n", static_cast<int>(i), exception.what());
			++failures;
		}
	}
	return (failures == 0 ? 0 : 1);
}
