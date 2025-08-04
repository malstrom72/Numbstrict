@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D "%~dp0"

FOR %%t IN (beta release) DO (
	SET "outDir=output\%%t"
	IF NOT EXIST "%%outDir%%" MKDIR "%%outDir%%"
	CALL tools\BuildCpp.cmd %%t native "%%outDir%%\smoke.exe" -I src tests\smoke.cpp src\Numbstrict.cpp src\Makaron.cpp || GOTO error
	"%%outDir%%\smoke.exe" >NUL || GOTO error
)
ECHO Build and tests completed
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
