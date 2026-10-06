#include "../src/Numbstrict.h"
#include "../src/Makaron.h"

int main() {
	return (Numbstrict::unitTest() && Makaron::unitTest()) ? 0 : 1;
}
