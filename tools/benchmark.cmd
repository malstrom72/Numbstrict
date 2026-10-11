@ECHO OFF
SETLOCAL
CD /D "%~dp0\.."
REM Builds and runs the benchmark of parsing and composing (tests\benchmark.cpp) in release and prints its table.
IF NOT EXIST output\release MKDIR output\release
CALL tools\BuildCpp.cmd release x64 output\release\benchmark.exe /I src ^
		tests\benchmark.cpp src\Numbstrict.cpp || EXIT /B 1
output\release\benchmark.exe
