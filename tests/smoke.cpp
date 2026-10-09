#include "../src/Numbstrict.h"
#include "../src/Makaron.h"
#include "QuietCrt.h"

int main() {
	quietCrt();
	return (Numbstrict::unitTest() && Makaron::unitTest()) ? 0 : 1;
}
