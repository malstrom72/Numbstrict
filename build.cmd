@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D "%~dp0"

FOR %%t IN (beta release) DO (
	SET "outDir=output\%%t"
	IF NOT EXIST "%%outDir%%" MKDIR "%%outDir%%"
	SET "CPP_OPTIONS=/std:c++14"
	CALL tools\BuildCpp.cmd %%t native "%%outDir%%\smoke.exe" -I src ^
		tests\smoke.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	"%%outDir%%\smoke.exe" >NUL || GOTO error
	SET "CPP_OPTIONS=/std:c++14"
	CALL tools\BuildCpp.cmd %%t native "%%outDir%%\MakaronCmd.exe" -I src ^
		tools\MakaronCmd.cpp src\Makaron.cpp || GOTO error
	SET "CPP_OPTIONS="
	SET "CPP_OPTIONS=/std:c++14"
	CALL tools\BuildCpp.cmd %%t native "%%outDir%%\HexDoubleToDecimal.exe" -I externals\ryu ^
		tools\HexDoubleToDecimal.cpp externals\ryu\ryu\d2s.c || GOTO error
	SET "CPP_OPTIONS="
	SET "CPP_OPTIONS=/std:c++14"
	CALL tools\BuildCpp.cmd %%t native "%%outDir%%\dd_parser_downscale_table.exe" dd_parser_downscale_table.cpp || GOTO error
	SET "CPP_OPTIONS="
	"%%outDir%%\dd_parser_downscale_table.exe" >NUL || GOTO error
)
ECHO Build and tests completed
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
