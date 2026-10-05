#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
	#include <windows.h>
#else
	#include <dirent.h>
	#include <sys/stat.h>
#endif

/*
	Runs a libFuzzer target over files instead of under libFuzzer: every file named on the command line, and every file
	below every directory named there. The build scripts link it with a fuzz target to replay the committed corpus with
	any compiler. tools/build_numbstrict_fuzz.* and tools/build_makaron_fuzz.* build the real fuzzers.
*/

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size);

static std::vector<std::string> listDirectory(const std::string& path) {
	std::vector<std::string> children;
#ifdef _WIN32
	WIN32_FIND_DATAA found;
	const HANDLE handle = FindFirstFileA((path + "\\*").c_str(), &found);
	if (handle == INVALID_HANDLE_VALUE) {
		return children;
	}
	do {
		const std::string name(found.cFileName);
		if (name != "." && name != "..") {
			children.push_back(path + "\\" + name);
		}
	} while (FindNextFileA(handle, &found));
	FindClose(handle);
#else
	DIR* const directory = opendir(path.c_str());
	if (directory == 0) {
		return children;
	}
	while (const dirent* entry = readdir(directory)) {
		const std::string name(entry->d_name);
		if (name != "." && name != "..") {
			children.push_back(path + "/" + name);
		}
	}
	closedir(directory);
#endif
	return children;
}

static bool isDirectory(const std::string& path) {
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat status;
	return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

static int runPath(const std::string& path) {
	if (isDirectory(path)) {
		const std::vector<std::string> children = listDirectory(path);
		int count = 0;
		for (size_t i = 0; i < children.size(); ++i) {
			count += runPath(children[i]);
		}
		return count;
	}
	std::ifstream file(path.c_str(), std::ios::binary);
	if (!file) {
		fprintf(stderr, "Could not open %s\n", path.c_str());
		return 0;
	}
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const size_t size = bytes.size();
	bytes.push_back(0);																									// so that `&bytes[0]` is valid for an empty file
	LLVMFuzzerTestOneInput(&bytes[0], size);
	return 1;
}

int main(int argc, char** argv) {
	LLVMFuzzerInitialize(&argc, &argv);
	int count = 0;
	for (int i = 1; i < argc; ++i) {
		const int ran = runPath(argv[i]);
		if (ran == 0) {
			fprintf(stderr, "No files found at %s\n", argv[i]);
			return 1;
		}
		count += ran;
	}
	printf("Fuzz target ran on %d files\n", count);
	return 0;
}
