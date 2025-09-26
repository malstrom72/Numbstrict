#include "Numbstrict.h"
#include "ryu/ryu.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
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

namespace {

struct Config {
	enum Scenario {
		DigitEmission,
		RoundingTies,
		ParserChunks,
		FPEnvScope
	};

	Scenario scenario;
	size_t count;
	uint64_t seed;
	bool runFloat;
	bool runDouble;
	bool useBatchGuard;
	bool verify;
	double minSeconds;
};

static volatile size_t g_sizeSink = 0;
static volatile double g_doubleSink = 0.0;
static volatile float g_floatSink = 0.0f;

static void printUsage(const char* exe) {
	std::cout << "Usage: " << exe << " [options]\n";
	std::cout << "Options:\n";
	std::cout << "\tcase=digitEmission      Measure formatter hot loop throughput (default)\n";
	std::cout << "\tcase=roundingTies      Stress rounding-edge values\n";
	std::cout << "\tcase=parserChunks      Measure parser chunk assembly throughput\n";
	std::cout << "\tcase=fpEnvScope       Compare per-call vs batched FP environment setup\n";
	std::cout << "\tfloat                  Only exercise float paths\n";
	std::cout << "\tdouble                 Only exercise double paths\n";
	std::cout << "\tcount=<value>          Number of samples to process (default 100000)\n";
	std::cout << "\tseed=<value>           RNG seed for dataset generation (default 123456789)\n";
	std::cout << "\tbatchGuard             Wrap operations in FloatStringBatchGuard\n";
	std::cout << "\tverify                 Compare results against reference implementations\n";
	std::cout << "\tduration=<seconds>     Minimum wall time to measure (default 12.0)\n";
	std::cout << "\t-h | -? | --help       Show this help text\n";
}

template<typename T> struct RandomBits;

template<> struct RandomBits<float> {
	typedef uint32_t Type;
	static Type toBits(float value) {
		Type bits = 0;
		std::memcpy(&bits, &value, sizeof value);
		return bits;
	}
	static float fromBits(Type bits) {
		float value = 0.0f;
		std::memcpy(&value, &bits, sizeof value);
		return value;
	}
};

template<> struct RandomBits<double> {
	typedef uint64_t Type;
	static Type toBits(double value) {
		Type bits = 0;
		std::memcpy(&bits, &value, sizeof value);
		return bits;
	}
	static double fromBits(Type bits) {
		double value = 0.0;
		std::memcpy(&value, &bits, sizeof value);
		return value;
	}
};

template<typename T>
static bool bitwiseEqual(T lhs, T rhs) {
	if (std::isnan(lhs) && std::isnan(rhs)) {
		return true;
	}
	typename RandomBits<T>::Type leftBits = RandomBits<T>::toBits(lhs);
	typename RandomBits<T>::Type rightBits = RandomBits<T>::toBits(rhs);
	return leftBits == rightBits;
}

template<typename T>
static std::vector<T> generateRandomValues(size_t count, uint64_t seed) {
	std::vector<T> values;
	values.reserve(count);
	std::mt19937_64 rng(seed);
	while (values.size() < count) {
		typename RandomBits<T>::Type bits = static_cast<typename RandomBits<T>::Type>(rng());
		values.push_back(RandomBits<T>::fromBits(bits));
	}
	return values;
}

static std::vector<double> generateRoundingDoubles(size_t count, uint64_t seed) {
	std::vector<double> values;
	values.reserve(count);
	std::mt19937_64 rng(seed);
	while (values.size() < count) {
		uint64_t bits = rng();
		double value = RandomBits<double>::fromBits(bits);
		if (std::isnan(value)) {
			continue;
		}
		double next = std::nextafter(value, std::numeric_limits<double>::infinity());
		double prev = std::nextafter(value, -std::numeric_limits<double>::infinity());
		values.push_back(prev);
		if (values.size() >= count) {
			break;
		}
		values.push_back(value);
		if (values.size() >= count) {
			break;
		}
		values.push_back(next);
	}
	values.resize(count);
	return values;
}

static std::vector<float> generateRoundingFloats(size_t count, uint64_t seed) {
	std::vector<float> values;
	values.reserve(count);
	std::mt19937_64 rng(seed);
	while (values.size() < count) {
		uint32_t bits = static_cast<uint32_t>(rng());
		float value = RandomBits<float>::fromBits(bits);
		if (std::isnan(value)) {
			continue;
		}
		float next = std::nextafter(value, std::numeric_limits<float>::infinity());
		float prev = std::nextafter(value, -std::numeric_limits<float>::infinity());
		values.push_back(prev);
		if (values.size() >= count) {
			break;
		}
		values.push_back(value);
		if (values.size() >= count) {
			break;
		}
		values.push_back(next);
	}
	values.resize(count);
	return values;
}

static std::vector<std::string> generateDecimalStrings(size_t count, uint64_t seed, bool isFloat) {
	static const char* kDoubleExtras[] = {
		"0.0",
		"-0.0",
		"1.0",
		"-1.0",
		"1.7976931348623157e308",
		"2.2250738585072014e-308",
		"5e-324",
		"-5e-324",
		"9.999999999999999e307",
		"-9.999999999999999e307"
	};
	static const char* kFloatExtras[] = {
		"0.0f",
		"-0.0f",
		"1.0f",
		"-1.0f",
		"3.4028235e38f",
		"1.17549435e-38f",
		"1e-45f",
		"-1e-45f",
		"9.999999e37f",
		"-9.999999e37f"
	};
	std::vector<std::string> texts;
	texts.reserve(count);
	if (isFloat) {
		const size_t extraCount = sizeof(kFloatExtras) / sizeof(kFloatExtras[0]);
		for (size_t i = 0; i < extraCount && texts.size() < count; ++i) {
			texts.push_back(kFloatExtras[i]);
		}
	} else {
		const size_t extraCount = sizeof(kDoubleExtras) / sizeof(kDoubleExtras[0]);
		for (size_t i = 0; i < extraCount && texts.size() < count; ++i) {
			texts.push_back(kDoubleExtras[i]);
		}
	}
	std::mt19937_64 rng(seed);
	std::uniform_int_distribution<int> digitDist(0, 9);
	std::uniform_int_distribution<int> integerCountDist(1, isFloat ? 9 : 20);
	std::uniform_int_distribution<int> fractionCountDist(0, isFloat ? 7 : 18);
	std::uniform_int_distribution<int> exponentMagnitudeDist(0, isFloat ? 60 : 320);
	std::bernoulli_distribution fractionChance(isFloat ? 0.75 : 0.85);
	std::bernoulli_distribution exponentChance(0.65);
	while (texts.size() < count) {
		std::string text;
		if (rng() & 1) {
			text.push_back('-');
		}
		const int integerDigits = integerCountDist(rng);
		for (int i = 0; i < integerDigits; ++i) {
			text.push_back(static_cast<char>('0' + digitDist(rng)));
		}
		if (fractionChance(rng)) {
			text.push_back('.');
			const int fractionDigits = fractionCountDist(rng);
			for (int i = 0; i < fractionDigits; ++i) {
				text.push_back(static_cast<char>('0' + digitDist(rng)));
			}
		}
		if (exponentChance(rng)) {
			text.push_back('e');
			if (rng() & 1) {
				text.push_back('-');
			} else {
				text.push_back('+');
			}
			int exponent = exponentMagnitudeDist(rng);
			if (!isFloat && (rng() & 1)) {
				exponent += exponentMagnitudeDist(rng);
			}
			if (exponent > 0) {
				std::ostringstream stream;
				stream << exponent;
				text.append(stream.str());
			} else {
				text.push_back('0');
			}
		}
		if (text.back() == '+' || text.back() == '-') {
			text.push_back('0');
		}
		texts.push_back(text);
	}
	texts.resize(count);
	return texts;
}

template<typename T> struct Sink;

template<> struct Sink<double> {
	static void store(double value) {
		g_doubleSink = value;
	}
};

template<> struct Sink<float> {
	static void store(float value) {
		g_floatSink = value;
	}
};

static std::string ryuFormatFloat(float value) {
	if (value == 0.0f) {
		return std::signbit(value) ? "-0.0" : "0.0";
	}
	if (std::isnan(value)) {
		return "nan";
	}
	if (std::isinf(value)) {
		return value < 0.0f ? "-inf" : "inf";
	}
	char buffer[32];
	int length = f2s_buffered_n(value, buffer);
	std::string text(buffer, static_cast<size_t>(length));
	bool negative = false;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		text.erase(0, 1);
	}
	size_t exponentPos = text.find('E');
	std::string mantissa = (exponentPos == std::string::npos ? text : text.substr(0, exponentPos));
	int exponent = 0;
	if (exponentPos != std::string::npos) {
		exponent = std::stoi(text.substr(exponentPos + 1));
	}
	size_t dotPos = mantissa.find('.');
	std::string integerPart = mantissa.substr(0, dotPos);
	std::string fractionalPart = (dotPos == std::string::npos ? std::string() : mantissa.substr(dotPos + 1));
	const int lowerBound = -6;
	const int upperBound = 10;
	std::string result;
	if (exponent < lowerBound || exponent >= upperBound) {
		result = integerPart;
		if (!fractionalPart.empty()) {
			result += '.';
			result += fractionalPart;
		} else {
			result += ".0";
		}
		result += 'e';
		result += (exponent >= 0 ? '+' : '-');
		const int magnitude = exponent >= 0 ? exponent : -exponent;
		result += std::to_string(magnitude);
	} else {
		std::string digits = integerPart + fractionalPart;
		int decimalPos = static_cast<int>(integerPart.size()) + exponent;
		if (decimalPos <= 0) {
			result = "0.";
			result.append(static_cast<size_t>(-decimalPos), '0');
			result += digits;
		} else if (decimalPos >= static_cast<int>(digits.size())) {
			result = digits;
			result.append(static_cast<size_t>(decimalPos - static_cast<int>(digits.size())), '0');
			result += ".0";
		} else {
			result = digits;
			result.insert(static_cast<size_t>(decimalPos), 1, '.');
		}
	}
	if (negative && result != "0.0") {
		result.insert(result.begin(), '-');
	}
	return result;
}

static std::string ryuFormatDouble(double value) {
	if (value == 0.0) {
		return std::signbit(value) ? "-0.0" : "0.0";
	}
	if (std::isnan(value)) {
		return "nan";
	}
	if (std::isinf(value)) {
		return value < 0.0 ? "-inf" : "inf";
	}
	char buffer[64];
	int length = d2s_buffered_n(value, buffer);
	std::string text(buffer, static_cast<size_t>(length));
	bool negative = false;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		text.erase(0, 1);
	}
	size_t exponentPos = text.find('E');
	std::string mantissa = (exponentPos == std::string::npos ? text : text.substr(0, exponentPos));
	int exponent = 0;
	if (exponentPos != std::string::npos) {
		exponent = std::stoi(text.substr(exponentPos + 1));
	}
	size_t dotPos = mantissa.find('.');
	std::string integerPart = mantissa.substr(0, dotPos);
	std::string fractionalPart = (dotPos == std::string::npos ? std::string() : mantissa.substr(dotPos + 1));
	const int lowerBound = -6;
	const int upperBound = 10;
	std::string result;
	if (exponent < lowerBound || exponent >= upperBound) {
		result = integerPart;
		if (!fractionalPart.empty()) {
			result += '.';
			result += fractionalPart;
		} else {
			result += ".0";
		}
		result += 'e';
		result += (exponent >= 0 ? '+' : '-');
		const int magnitude = exponent >= 0 ? exponent : -exponent;
		result += std::to_string(magnitude);
	} else {
		std::string digits = integerPart + fractionalPart;
		int decimalPos = static_cast<int>(integerPart.size()) + exponent;
		if (decimalPos <= 0) {
			result = "0.";
			result.append(static_cast<size_t>(-decimalPos), '0');
			result += digits;
		} else if (decimalPos >= static_cast<int>(digits.size())) {
			result = digits;
			result.append(static_cast<size_t>(decimalPos - static_cast<int>(digits.size())), '0');
			result += ".0";
		} else {
			result = digits;
			result.insert(static_cast<size_t>(decimalPos), 1, '.');
		}
	}
	if (negative && result != "0.0") {
		result.insert(result.begin(), '-');
	}
	return result;
}

template<typename T, typename Formatter, typename Oracle>
static bool runFormatCase(const std::vector<T>& values, bool useBatchGuard, bool verify, double minSeconds, Formatter formatter, Oracle oracle, double& nsPerValue, size_t& iterations, double& totalSeconds) {
	const std::chrono::steady_clock::time_point loopStart = std::chrono::steady_clock::now();
	double accumulatedNs = 0.0;
	size_t accumulatedLength = 0;
	iterations = 0;
	while (true) {
		const bool performVerify = verify && iterations == 0;
		size_t iterationLength = 0;
		const std::chrono::steady_clock::time_point iterationStart = std::chrono::steady_clock::now();
		bool ok = true;
		if (useBatchGuard) {
			Numbstrict::FloatStringBatchGuard guard;
			ok = formatter(values, performVerify, oracle, iterationLength);
		} else {
			ok = formatter(values, performVerify, oracle, iterationLength);
		}
		const std::chrono::steady_clock::time_point iterationEnd = std::chrono::steady_clock::now();
		if (!ok) {
			return false;
		}
		accumulatedLength += iterationLength;
		accumulatedNs += std::chrono::duration<double, std::nano>(iterationEnd - iterationStart).count();
		++iterations;
		const double elapsedSeconds = std::chrono::duration<double>(iterationEnd - loopStart).count();
		if (elapsedSeconds >= minSeconds) {
			break;
		}
	}
	g_sizeSink = accumulatedLength;
	totalSeconds = accumulatedNs / 1.0e9;
	nsPerValue = accumulatedNs / (static_cast<double>(iterations) * static_cast<double>(values.size()));
	return true;
}

template<typename T, typename Parser, typename Oracle>
static bool runParseCase(const std::vector<std::string>& texts, bool useBatchGuard, bool verify, double minSeconds, Parser parser, Oracle oracle, double& nsPerValue, size_t& iterations, double& totalSeconds) {
	const std::chrono::steady_clock::time_point loopStart = std::chrono::steady_clock::now();
	double accumulatedNs = 0.0;
	iterations = 0;
	while (true) {
		const bool performVerify = verify && iterations == 0;
		const std::chrono::steady_clock::time_point iterationStart = std::chrono::steady_clock::now();
		bool ok = true;
		if (useBatchGuard) {
			Numbstrict::FloatStringBatchGuard guard;
			ok = parser(texts, performVerify, oracle);
		} else {
			ok = parser(texts, performVerify, oracle);
		}
		const std::chrono::steady_clock::time_point iterationEnd = std::chrono::steady_clock::now();
		if (!ok) {
			return false;
		}
		accumulatedNs += std::chrono::duration<double, std::nano>(iterationEnd - iterationStart).count();
		++iterations;
		const double elapsedSeconds = std::chrono::duration<double>(iterationEnd - loopStart).count();
		if (elapsedSeconds >= minSeconds) {
			break;
		}
	}
	totalSeconds = accumulatedNs / 1.0e9;
	nsPerValue = accumulatedNs / (static_cast<double>(iterations) * static_cast<double>(texts.size()));
	return true;
}

static std::string formatDuration(double value) {
	std::ostringstream stream;
	stream.setf(std::ios::fixed);
	stream << std::setprecision(2) << value;
	return stream.str();
}

template<typename T>
static void printMeasurement(const char* label, size_t count, double nsPerValue, size_t iterations, double totalSeconds) {
	std::cout << label << ": " << formatDuration(nsPerValue) << " ns/value across "
		<< iterations << " iterations (" << formatDuration(totalSeconds) << " s total, dataset="
		<< count << ")" << std::endl;
}

static void normalizeExponent(std::string& text) {
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == 'e' || c == 'E') {
			text[i] = 'e';
			if (i + 1 < text.size()) {
				char next = text[i + 1];
				if (next != '+' && next != '-') {
					text.insert(i + 1, 1, '+');
				}
			}
			return;
		}
	}
}

static bool equalsIgnoringCase(const Numbstrict::String& lhs, const std::string& rhs) {
	std::string normalizedLhs(lhs.begin(), lhs.end());
	std::string normalizedRhs = rhs;
	normalizeExponent(normalizedLhs);
	normalizeExponent(normalizedRhs);
	if (normalizedLhs.size() != normalizedRhs.size()) {
		return false;
	}
	for (size_t i = 0; i < normalizedLhs.size(); ++i) {
		const unsigned char leftChar = static_cast<unsigned char>(normalizedLhs[i]);
		const unsigned char rightChar = static_cast<unsigned char>(normalizedRhs[i]);
		if (leftChar == rightChar) {
			continue;
		}
		if (std::tolower(leftChar) != std::tolower(rightChar)) {
			return false;
		}
	}
	return true;
}

static bool formatterBodyDouble(const std::vector<double>& values, bool verify, std::string (*oracle)(double), size_t& totalLength) {
	for (size_t i = 0; i < values.size(); ++i) {
		const Numbstrict::String text = Numbstrict::doubleToString(values[i]);
		totalLength += text.size();
		if (verify && std::isfinite(values[i])) {
			const std::string expected = oracle(values[i]);
			if (!equalsIgnoringCase(text, expected)) {
				std::ostringstream valueStream;
				valueStream.setf(std::ios::scientific, std::ios::floatfield);
				valueStream << std::setprecision(17) << values[i];
				std::cerr << "Mismatch for value " << valueStream.str() << "\n";
				std::cerr << "Numbstrict: " << text << "\n";
				std::cerr << "Expected:  " << expected << "\n";
				return false;
			}
		}
	}
	return true;
}

static bool formatterBodyFloat(const std::vector<float>& values, bool verify, std::string (*oracle)(float), size_t& totalLength) {
	for (size_t i = 0; i < values.size(); ++i) {
		const Numbstrict::String text = Numbstrict::floatToString(values[i]);
		totalLength += text.size();
		if (verify && std::isfinite(values[i])) {
			const std::string expected = oracle(values[i]);
			if (!equalsIgnoringCase(text, expected)) {
				std::ostringstream valueStream;
				valueStream.setf(std::ios::scientific, std::ios::floatfield);
				valueStream << std::setprecision(9) << values[i];
				std::cerr << "Mismatch for value " << valueStream.str() << "\n";
				std::cerr << "Numbstrict: " << text << "\n";
				std::cerr << "Expected:  " << expected << "\n";
				return false;
			}
		}
	}
	return true;
}

static bool parserBodyDouble(const std::vector<std::string>& texts, bool verify, double (*oracle)(const std::string&)) {
	for (size_t i = 0; i < texts.size(); ++i) {
		const Numbstrict::String text = texts[i];
		const double value = Numbstrict::stringToDouble(text);
		Sink<double>::store(value);
		if (verify) {
			const double expected = oracle(text);
			if (!bitwiseEqual(value, expected)) {
				std::ostringstream numbstrictStream;
				numbstrictStream.setf(std::ios::scientific, std::ios::floatfield);
				numbstrictStream << std::setprecision(17) << value;
				std::ostringstream expectedStream;
				expectedStream.setf(std::ios::scientific, std::ios::floatfield);
				expectedStream << std::setprecision(17) << expected;
				std::cerr << "Parser mismatch for " << text << "\n";
				std::cerr << "Numbstrict: " << numbstrictStream.str() << "\n";
				std::cerr << "Expected:  " << expectedStream.str() << "\n";
				return false;
			}
		}
	}
	return true;
}

static bool parserBodyFloat(const std::vector<std::string>& texts, bool verify, float (*oracle)(const std::string&)) {
	for (size_t i = 0; i < texts.size(); ++i) {
		const Numbstrict::String text = texts[i];
		const float value = Numbstrict::stringToFloat(text);
		Sink<float>::store(value);
		if (verify) {
			const float expected = oracle(text);
			if (!bitwiseEqual(value, expected)) {
				std::ostringstream numbstrictStream;
				numbstrictStream.setf(std::ios::scientific, std::ios::floatfield);
				numbstrictStream << std::setprecision(9) << value;
				std::ostringstream expectedStream;
				expectedStream.setf(std::ios::scientific, std::ios::floatfield);
				expectedStream << std::setprecision(9) << expected;
				std::cerr << "Parser mismatch for " << text << "\n";
				std::cerr << "Numbstrict: " << numbstrictStream.str() << "\n";
				std::cerr << "Expected:  " << expectedStream.str() << "\n";
				return false;
			}
		}
	}
	return true;
}

static double oracleStrtod(const std::string& text) {
	return std::strtod(text.c_str(), 0);
}

static float oracleStrtof(const std::string& text) {
	return std::strtof(text.c_str(), 0);
}

static bool runDigitEmission(const Config& config) {
	bool ok = true;
	const size_t pathCount = (config.runDouble ? 1u : 0u) + (config.runFloat ? 1u : 0u);
	const double perPathSeconds = pathCount > 0 ? config.minSeconds / static_cast<double>(pathCount) : config.minSeconds;
	if (config.runDouble) {
		std::vector<double> values = generateRandomValues<double>(config.count, config.seed);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<double>(values, config.useBatchGuard, config.verify, perPathSeconds, formatterBodyDouble, ryuFormatDouble, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<double>("doubleToString", values.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	if (config.runFloat && ok) {
		std::vector<float> values = generateRandomValues<float>(config.count, config.seed + 1);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<float>(values, config.useBatchGuard, config.verify, perPathSeconds, formatterBodyFloat, ryuFormatFloat, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<float>("floatToString", values.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	return ok;
}

static bool runRoundingTies(const Config& config) {
	bool ok = true;
	const size_t pathCount = (config.runDouble ? 1u : 0u) + (config.runFloat ? 1u : 0u);
	const double perPathSeconds = pathCount > 0 ? config.minSeconds / static_cast<double>(pathCount) : config.minSeconds;
	if (config.runDouble) {
		std::vector<double> values = generateRoundingDoubles(config.count, config.seed);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<double>(values, config.useBatchGuard, config.verify, perPathSeconds, formatterBodyDouble, ryuFormatDouble, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<double>("doubleToString (ties)", values.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	if (config.runFloat && ok) {
		std::vector<float> values = generateRoundingFloats(config.count, config.seed + 1);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<float>(values, config.useBatchGuard, config.verify, perPathSeconds, formatterBodyFloat, ryuFormatFloat, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<float>("floatToString (ties)", values.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	return ok;
}

static bool runParserChunks(const Config& config) {
	bool ok = true;
	const size_t pathCount = (config.runDouble ? 1u : 0u) + (config.runFloat ? 1u : 0u);
	const double perPathSeconds = pathCount > 0 ? config.minSeconds / static_cast<double>(pathCount) : config.minSeconds;
	if (config.runDouble) {
		std::vector<std::string> texts = generateDecimalStrings(config.count, config.seed, false);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runParseCase<double>(texts, config.useBatchGuard, config.verify, perPathSeconds, parserBodyDouble, oracleStrtod, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<double>("stringToDouble", texts.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	if (config.runFloat && ok) {
		std::vector<std::string> texts = generateDecimalStrings(config.count, config.seed + 1, true);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runParseCase<float>(texts, config.useBatchGuard, config.verify, perPathSeconds, parserBodyFloat, oracleStrtof, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<float>("stringToFloat", texts.size(), nsPerValue, iterations, totalSeconds);
		}
	}
	return ok;
}

static bool runFPEnvScope(const Config& config) {
	bool ok = true;
	if (config.runDouble) {
		std::vector<double> values = generateRandomValues<double>(config.count, config.seed);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<double>(values, false, config.verify, config.minSeconds, formatterBodyDouble, ryuFormatDouble, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<double>("doubleToString (per-call guard)", values.size(), nsPerValue, iterations, totalSeconds);
			double batchedNs = 0.0;
			double batchedSeconds = 0.0;
			size_t batchedIterations = 0;
			if (!runFormatCase<double>(values, true, config.verify, config.minSeconds, formatterBodyDouble, ryuFormatDouble, batchedNs, batchedIterations, batchedSeconds)) {
				ok = false;
			} else {
				printMeasurement<double>("doubleToString (batched guard)", values.size(), batchedNs, batchedIterations, batchedSeconds);
			}
		}
	}
	if (config.runFloat && ok) {
		std::vector<float> values = generateRandomValues<float>(config.count, config.seed + 1);
		double nsPerValue = 0.0;
		double totalSeconds = 0.0;
		size_t iterations = 0;
		if (!runFormatCase<float>(values, false, config.verify, config.minSeconds, formatterBodyFloat, ryuFormatFloat, nsPerValue, iterations, totalSeconds)) {
			ok = false;
		} else {
			printMeasurement<float>("floatToString (per-call guard)", values.size(), nsPerValue, iterations, totalSeconds);
			double batchedNs = 0.0;
			double batchedSeconds = 0.0;
			size_t batchedIterations = 0;
			if (!runFormatCase<float>(values, true, config.verify, config.minSeconds, formatterBodyFloat, ryuFormatFloat, batchedNs, batchedIterations, batchedSeconds)) {
				ok = false;
			} else {
				printMeasurement<float>("floatToString (batched guard)", values.size(), batchedNs, batchedIterations, batchedSeconds);
			}
		}
	}
	return ok;
}



} // namespace

int main(int argc, char** argv) {
	Config config;
	config.scenario = Config::DigitEmission;
	config.count = 100000;
	config.seed = 123456789u;
	config.runFloat = true;
	config.runDouble = true;
	config.useBatchGuard = false;
	config.verify = false;
	config.minSeconds = 12.0;
	for (int i = 1; i < argc; ++i) {
		const char* arg = argv[i];
		if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "-?") == 0) {
			printUsage(argv[0]);
			return 0;
		}
		if (std::strcmp(arg, "float") == 0) {
			config.runDouble = false;
			continue;
		}
		if (std::strcmp(arg, "double") == 0) {
			config.runFloat = false;
			continue;
		}
		if (std::strcmp(arg, "batchGuard") == 0) {
			config.useBatchGuard = true;
			continue;
		}
		if (std::strcmp(arg, "verify") == 0) {
			config.verify = true;
			continue;
		}
		if (std::strncmp(arg, "case=", 5) == 0) {
			const std::string value(arg + 5);
			if (value == "digitEmission") {
				config.scenario = Config::DigitEmission;
				continue;
			}
			if (value == "roundingTies") {
				config.scenario = Config::RoundingTies;
				continue;
			}
			if (value == "parserChunks") {
				config.scenario = Config::ParserChunks;
				continue;
			}
			if (value == "fpEnvScope") {
				config.scenario = Config::FPEnvScope;
				continue;
			}
			std::cerr << "Unknown case: " << value << std::endl;
			printUsage(argv[0]);
			return 1;
		}
		if (std::strncmp(arg, "count=", 6) == 0) {
			config.count = static_cast<size_t>(std::strtoull(arg + 6, 0, 10));
			if (config.count == 0) {
				std::cerr << "count must be greater than zero" << std::endl;
				return 1;
			}
			continue;
		}
		if (std::strncmp(arg, "seed=", 5) == 0) {
			config.seed = std::strtoull(arg + 5, 0, 10);
			continue;
		}
		if (std::strncmp(arg, "duration=", 9) == 0) {
			config.minSeconds = std::strtod(arg + 9, 0);
			if (config.minSeconds < 6.0) {
				std::cerr << "duration must be at least 6 seconds" << std::endl;
				return 1;
			}
			continue;
		}
		std::cerr << "Unknown argument: " << arg << std::endl;
		printUsage(argv[0]);
		return 1;
	}
	bool ok = false;
	switch (config.scenario) {
	case Config::DigitEmission:
		ok = runDigitEmission(config);
		break;
	case Config::RoundingTies:
		ok = runRoundingTies(config);
		break;
	case Config::ParserChunks:
		ok = runParserChunks(config);
		break;
	case Config::FPEnvScope:
		ok = runFPEnvScope(config);
		break;
	}
	return ok ? 0 : 1;
}
