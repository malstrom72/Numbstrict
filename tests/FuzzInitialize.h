/*
	Defines `LLVMFuzzerInitialize` for a fuzz target: it turns off the CRT dialogs (see `QuietCrt.h`), so a failure
	ends the run with a message instead of waiting for someone to click. Include it from exactly one source file per
	fuzz target.
*/
#ifndef FuzzInitialize_h
#define FuzzInitialize_h

#include "QuietCrt.h"

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
	(void)argc;
	(void)argv;
	quietCrt();
	return 0;
}

#endif
