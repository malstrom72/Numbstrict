/*
	Defines `LLVMFuzzerInitialize` for a fuzz target. On Windows it sends CRT assert and error reports to stderr and
	disables the abort dialog, so a failure ends the run with a message instead of waiting for someone to click.
	Include it from exactly one source file per fuzz target.
*/
#ifndef FuzzInitialize_h
#define FuzzInitialize_h

#ifdef _WIN32
	#include <crtdbg.h>
	#include <stdlib.h>
#endif

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
	(void)argc;
	(void)argv;
#ifdef _WIN32
	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
	return 0;
}

#endif
