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
	IF EXIST externals\ryu\NUL (
		REM Intentionally avoid /std:c++14 for older MSVC (v140)
		CALL tools\BuildCpp.cmd %%t x64 "!outDir!\HexDoubleToDecimal.exe" /I externals\ryu ^
				tools\HexDoubleToDecimal.cpp externals\ryu\ryu\d2s.c || GOTO error
		SET "CPP_OPTIONS="
	)
)
ECHO Build and tests completed
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
