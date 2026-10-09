/*
	`quietCrt()` sends CRT assert and error reports to stderr and disables the abort dialog on Windows, so a failing
	test ends with a message instead of waiting for someone to click. It does nothing elsewhere.
*/
#ifndef QuietCrt_h
#define QuietCrt_h

#ifdef _WIN32
	#include <crtdbg.h>
	#include <stdlib.h>
#endif

inline void quietCrt() {
#ifdef _WIN32
	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}

#endif
