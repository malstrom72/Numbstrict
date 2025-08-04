@ECHO OFF
CD /D "%~dp0\.."
IF NOT EXIST output MKDIR output
SET CPP_OPTIONS=/fsanitize:fuzzer,address
CALL tools\BuildCpp.cmd beta x64 output\NumbstrictFuzz.exe tests\NumbstrictFuzz.cpp src\Numbstrict.cpp || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
