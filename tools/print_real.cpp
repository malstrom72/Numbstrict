#include "../src/Numbstrict.h"
#include <iostream>
int main(){
	std::cout << Numbstrict::doubleToString(9.88131291682493088353138e-324) << "\n";
	std::cout << Numbstrict::doubleToString(4.94065645841246544176569e-324) << "\n";
	std::cout << Numbstrict::doubleToString(9.88131291682493088353138e-324 * 10) << "\n";
	return 0;
}
