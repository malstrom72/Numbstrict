@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D "%~dp0"

FOR %%t IN (beta release) DO (
	SET "outDir=output\%%t"
	IF NOT EXIST "!outDir!" MKDIR "!outDir!"
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\smoke.exe" /I src ^
		tests\smoke.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	"!outDir!\smoke.exe" >NUL || GOTO error
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\MakaronCmd.exe" /I src ^
		tools\MakaronCmd.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\HexDoubleToDecimal.exe" /I externals\ryu ^
		tools\HexDoubleToDecimal.cpp externals\ryu\ryu\d2s.c || GOTO error
	SET "CPP_OPTIONS="
	REM Build ryu comparison test (like build.sh)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\compareWithRyu.exe" /I src /I externals\ryu ^
		tests\compareWithRyu.cpp src\Numbstrict.cpp src\Makaron.cpp ^
		externals\ryu\ryu\d2s.c externals\ryu\ryu\f2s.c || GOTO error
	"!outDir!\compareWithRyu.exe" float >NUL || GOTO error
	SET "CPP_OPTIONS="
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\dd_parser_downscale_table.exe" dd_parser_downscale_table.cpp || GOTO error
	SET "CPP_OPTIONS="
	"!outDir!\dd_parser_downscale_table.exe" >NUL || GOTO error
)
ECHO Build and tests completed
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
