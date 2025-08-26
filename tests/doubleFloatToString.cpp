#include "../src/Numbstrict.h"
#include <cassert>
#include <limits>
#include <cmath>

int main() {
	struct DoubleTest { double value; const char* expected; };
	const DoubleTest doubleTests[] = {
		{0.0, "0.0"},
		{-1.0, "-1.0"},
		{1.12345689101112133911897, "1.1234568910111213"},
		{999.999999999999886313162, "999.9999999999999"},
		{123456789.12345677614212, "123456789.12345678"},
		{123400000000000000000.0, "1.234e+20"},
		{1.23400000000000005948965e+40, "1.234e+40"},
		{1.23400000000000008905397e+80, "1.234e+80"},
		{8.76499999999999967951181e-20, "8.765e-20"},
		{-8.76499999999999944958345e-40, "-8.765e-40"},
		{8.76500000000000034073598e-80, "8.765e-80"},
		{4.94065645841246544176569e-324, "5.0e-324"},
		{9.88131291682493088353138e-324, "1.0e-323"},
		{1.79769313486231570814527e+308, "1.7976931348623157e+308"},
		{1.79769313486231550856124e+308, "1.7976931348623155e+308"},
		{std::numeric_limits<double>::infinity(), "inf"},
		{std::numeric_limits<double>::quiet_NaN(), "nan"},
	};
	for (const DoubleTest &t : doubleTests) {
		const auto s = Numbstrict::doubleToString(t.value);
		assert(s == t.expected);
		const double round = Numbstrict::stringToDouble(s);
		if (std::isnan(t.value)) {
			assert(std::isnan(round));
		} else {
			assert(round == t.value);
		}
		static_cast<void>(round);
	}

	struct FloatTest { float value; const char* expected; };
	const FloatTest floatTests[] = {
		{0.0f, "0.0"},
		{-1.0f, "-1.0"},
		{1.12345683575f, "1.1234568"},
		{1000.0f, "1000.0"},
		{123456792.0f, "123456790.0"},
		{1.2340000198e+20f, "1.234e+20"},
		{8.7650002873e-20f, "8.765e-20"},
		{-8.76499577749e-40f, "-8.765e-40"},
		{1.40129846432e-45f, "1.0e-45"},
		{2.80259692865e-45f, "3.0e-45"},
		{3.40282326356e+38f, "3.4028233e+38"},
		{3.40282346638e+38f, "3.4028234e+38"},
		{std::numeric_limits<float>::infinity(), "inf"},
		{std::numeric_limits<float>::quiet_NaN(), "nan"},
	};
	for (const FloatTest &t : floatTests) {
		const auto s = Numbstrict::floatToString(t.value);
		assert(s == t.expected);
		const float round = Numbstrict::stringToFloat(s);
		if (std::isnan(t.value)) {
			assert(std::isnan(round));
		} else {
			assert(round == t.value);
		}
		static_cast<void>(round);
	}
	return 0;
}
