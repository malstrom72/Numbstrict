@ECHO OFF
CD /D "%~dp0\.."
IF NOT EXIST output MKDIR output
SET CPP_OPTIONS=/I src /fsanitize:fuzzer,address
CALL tools\BuildCpp.cmd beta x64 output\MakaronFuzz.exe tests\MakaronFuzz.cpp src\Makaron.cpp || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
