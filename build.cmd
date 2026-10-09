@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D "%~dp0"

REM The committed fuzz corpora and past crash inputs, replayed through the fuzz targets in beta builds
IF EXIST output\fuzzReplay RMDIR /S /Q output\fuzzReplay
MKDIR output\fuzzReplay
tar -xzf tests\fuzz\numbstrictCorpus.tar.gz -C output\fuzzReplay || GOTO error
tar -xzf tests\fuzz\makaronCorpus.tar.gz -C output\fuzzReplay || GOTO error
tar -xzf tests\fuzz\realConversionCorpus.tar.gz -C output\fuzzReplay || GOTO error

FOR %%t IN (beta release) DO (
	SET "outDir=output\%%t"
	IF NOT EXIST "!outDir!" MKDIR "!outDir!"
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\smoke.exe" /I src ^
			tests\smoke.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	"!outDir!\smoke.exe" >NUL || GOTO error
	REM 32-bit x86 has its own floating-point environment code path (x87 + SSE)
	CALL tools\BuildCpp.cmd %%t x86 "!outDir!\smoke_x86.exe" /I src ^
			tests\smoke.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	"!outDir!\smoke_x86.exe" >NUL || GOTO error
	IF "%%t"=="beta" (
		CALL tools\BuildCpp.cmd %%t x64 "!outDir!\NumbstrictFuzzReplay.exe" ^
				tests\NumbstrictFuzz.cpp tests\FuzzMain.cpp src\Numbstrict.cpp || GOTO error
		SET "CPP_OPTIONS="
		"!outDir!\NumbstrictFuzzReplay.exe" output\fuzzReplay\numbstrict >NUL || GOTO error
		CALL tools\BuildCpp.cmd %%t x64 "!outDir!\RealConversionFuzzReplay.exe" ^
				tests\RealConversionFuzz.cpp tests\FuzzMain.cpp src\Numbstrict.cpp || GOTO error
		SET "CPP_OPTIONS="
		"!outDir!\RealConversionFuzzReplay.exe" output\fuzzReplay\realConversion >NUL || GOTO error
		CALL tools\BuildCpp.cmd %%t x64 "!outDir!\MakaronFuzzReplay.exe" /I src ^
				tests\MakaronFuzz.cpp tests\FuzzMain.cpp src\Makaron.cpp || GOTO error
		SET "CPP_OPTIONS="
		"!outDir!\MakaronFuzzReplay.exe" output\fuzzReplay\makaron tests\fuzz\makaronCrashes >NUL || GOTO error
	)
	REM Intentionally avoid /std:c++14 for older MSVC (v140)
	CALL tools\BuildCpp.cmd %%t x64 "!outDir!\doubleFloatToString.exe" /I src ^
			tests\doubleFloatToString.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	"!outDir!\doubleFloatToString.exe" >NUL || GOTO error
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
