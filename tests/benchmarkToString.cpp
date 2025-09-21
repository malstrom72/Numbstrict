#include "Numbstrict.h"
#include "ryu/ryu.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

static volatile size_t g_sink = 0;

static void printUsage(const char* exe) {
	std::cout << "Usage: " << exe << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "\tfloat				  Only run float benchmarks" << std::endl;
	std::cout << "\tdouble				  Only run double benchmarks" << std::endl;
	std::cout << "\tcount=<value>		  Number of values to convert (default 1000000)" << std::endl;
	std::cout << "\tseed=<value>		  Seed for random value generation (default 123456789)" << std::endl;
	std::cout << "\t-h | -? | --help	  Show this help" << std::endl;
}

template<typename T> struct RandomBits;

template<> struct RandomBits<float> {
	typedef uint32_t Type;
	static Type next(std::mt19937_64& rng) { return static_cast<Type>(rng()); }
};

template<> struct RandomBits<double> {
	typedef uint64_t Type;
	static Type next(std::mt19937_64& rng) { return static_cast<Type>(rng()); }
};

template<typename T>
static size_t toSinkValue(T value) {
	typename RandomBits<T>::Type bits = 0;
	std::memcpy(&bits, &value, sizeof value);
	return static_cast<size_t>(bits);
}

template<typename T>
static T buildFromBits(typename RandomBits<T>::Type bits) {
	T value;
	std::memcpy(&value, &bits, sizeof value);
	return value;
}

template<typename T>
static std::vector<T> generateValues(size_t count, uint64_t seed) {
	static const T specials[] = {
		static_cast<T>(0.0),
		-static_cast<T>(0.0),
		static_cast<T>(1.0),
		-static_cast<T>(1.0),
		std::numeric_limits<T>::min(),
		std::numeric_limits<T>::max(),
		std::numeric_limits<T>::lowest(),
		std::numeric_limits<T>::denorm_min(),
		std::numeric_limits<T>::infinity(),
		-std::numeric_limits<T>::infinity(),
		std::numeric_limits<T>::quiet_NaN()
	};
	const size_t specialCount = sizeof(specials) / sizeof(specials[0]);
	std::vector<T> values;
	values.reserve(count > specialCount ? count : specialCount);
	const size_t limit = std::min(count, specialCount);
	for (size_t i = 0; i < limit; ++i) {
		values.push_back(specials[i]);
	}
	std::mt19937_64 rng(seed);
	while (values.size() < count) {
		values.push_back(buildFromBits<T>(RandomBits<T>::next(rng)));
	}
	return values;
}
static Numbstrict::String numbstrictFloatToString(float value) {
	return Numbstrict::floatToString(value);
}

static Numbstrict::String numbstrictDoubleToString(double value) {
	return Numbstrict::doubleToString(value);
}

static float numbstrictStringToFloat(const Numbstrict::String& text) {
	return Numbstrict::stringToFloat(text);
}

static float stdStrtof(const Numbstrict::String& text) {
	return std::strtof(text.c_str(), 0);
}

static float stringstreamStringToFloat(const Numbstrict::String& text) {
	std::istringstream stream(text);
	float value = 0.0f;
	stream >> value;
	return value;
}

static double numbstrictStringToDouble(const Numbstrict::String& text) {
	return Numbstrict::stringToDouble(text);
}

static double stdStrtod(const Numbstrict::String& text) {
	return std::strtod(text.c_str(), 0);
}

static double stringstreamStringToDouble(const Numbstrict::String& text) {
	std::istringstream stream(text);
	double value = 0.0;
	stream >> value;
	return value;
}

static std::string ryuFloatToString(float value) {
	char buffer[32];
	int length = f2s_buffered_n(value, buffer);
	return std::string(buffer, static_cast<size_t>(length));
}

static std::string ryuDoubleToString(double value) {
	char buffer[64];
	int length = d2s_buffered_n(value, buffer);
	return std::string(buffer, static_cast<size_t>(length));
}

template<typename T>
static std::string standardToString(T value) {
	std::ostringstream stream;
	stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
	stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
	return stream.str();
}

template<typename T, typename Func>
static void runBenchmark(const std::vector<T>& values, const char* label, Func func) {
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	size_t totalLength = 0;
	for (size_t i = 0; i < values.size(); ++i) {
		auto text = func(values[i]);
		totalLength += text.size();
	}
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	g_sink = totalLength;
	const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
	const double perValueNs = values.empty() ? 0.0 : (elapsedMs * 1000000.0 / static_cast<double>(values.size()));
	std::cout << label << ": " << elapsedMs << " ms";
	if (!values.empty()) {
		std::cout << " (" << perValueNs << " ns/value)";
	}
	std::cout << std::endl;
}

template<typename T, typename Func>
static void runStringToRealBenchmark(const std::vector<Numbstrict::String>& values, const char* label, Func func) {
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	size_t checksum = 0;
	for (size_t i = 0; i < values.size(); ++i) {
		const T parsed = func(values[i]);
		checksum += toSinkValue(parsed);
	}
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	g_sink = checksum;
	const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
	const double perValueNs = values.empty() ? 0.0 : (elapsedMs * 1000000.0 / static_cast<double>(values.size()));
	std::cout << label << ": " << elapsedMs << " ms";
	if (!values.empty()) {
		std::cout << " (" << perValueNs << " ns/value)";
	}
	std::cout << std::endl;
}

int main(int argc, const char** argv) {
	size_t count = 1000000;
	uint64_t seed = 123456789ull;
	bool runFloat = true;
	bool runDouble = true;

	for (int i = 1; i < argc; ++i) {
		std::string arg(argv[i]);
		if (arg == "float") {
			runDouble = false;
		} else if (arg == "double") {
			runFloat = false;
		} else if (arg.compare(0, 6, "count=") == 0) {
			count = static_cast<size_t>(std::strtoull(arg.c_str() + 6, 0, 10));
		} else if (arg.compare(0, 5, "seed=") == 0) {
			seed = std::strtoull(arg.c_str() + 5, 0, 10);
		} else if (arg == "-h" || arg == "-?" || arg == "--help") {
			printUsage(argv[0]);
			return 0;
		} else {
			std::cout << "Unknown option: " << arg << std::endl;
			printUsage(argv[0]);
			return 1;
		}
	}

	if (!runFloat && !runDouble) {
		runFloat = true;
		runDouble = true;
	}

	if (runDouble) {
		std::vector<double> values = generateValues<double>(count, seed);
		std::cout << "double benchmarks (" << values.size() << " values)" << std::endl;
		{
			Numbstrict::FloatStringBatchGuard guard;
			runBenchmark(values, "Numbstrict::doubleToString", numbstrictDoubleToString);
		}
		runBenchmark(values, "Ryu d2s", ryuDoubleToString);
		runBenchmark(values, "std::ostringstream<double>", standardToString<double>);
		std::vector<Numbstrict::String> doubleStrings;
		doubleStrings.reserve(values.size());
		{
			Numbstrict::FloatStringBatchGuard guard;
			for (size_t i = 0; i < values.size(); ++i) {
				doubleStrings.push_back(numbstrictDoubleToString(values[i]));
			}
		}
		std::cout << "string to double benchmarks" << std::endl;
		{
			Numbstrict::FloatStringBatchGuard guard;
			runStringToRealBenchmark<double>(doubleStrings, "Numbstrict::stringToDouble", numbstrictStringToDouble);
		}
		runStringToRealBenchmark<double>(doubleStrings, "std::strtod", stdStrtod);
		runStringToRealBenchmark<double>(doubleStrings, "std::istringstream<double>", stringstreamStringToDouble);
		std::cout << std::endl;
	}

	if (runFloat) {
		std::vector<float> values = generateValues<float>(count, seed);
		std::cout << "float benchmarks (" << values.size() << " values)" << std::endl;
		{
			Numbstrict::FloatStringBatchGuard guard;
			runBenchmark(values, "Numbstrict::floatToString", numbstrictFloatToString);
		}
		runBenchmark(values, "Ryu f2s", ryuFloatToString);
		runBenchmark(values, "std::ostringstream<float>", standardToString<float>);
		std::vector<Numbstrict::String> floatStrings;
		floatStrings.reserve(values.size());
		{
			Numbstrict::FloatStringBatchGuard guard;
			for (size_t i = 0; i < values.size(); ++i) {
				floatStrings.push_back(numbstrictFloatToString(values[i]));
			}
		}
		std::cout << "string to float benchmarks" << std::endl;
		{
			Numbstrict::FloatStringBatchGuard guard;
			runStringToRealBenchmark<float>(floatStrings, "Numbstrict::stringToFloat", numbstrictStringToFloat);
		}
		runStringToRealBenchmark<float>(floatStrings, "std::strtof", stdStrtof);
		runStringToRealBenchmark<float>(floatStrings, "std::istringstream<float>", stringstreamStringToFloat);
	}

	return 0;
}
