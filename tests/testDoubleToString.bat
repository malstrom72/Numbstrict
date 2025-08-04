@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

PUSHD %~dp0

SET target=%~1
SET model=%~2
IF "%target%"=="" SET target=debug
IF "%model%"=="" SET model=64

CALL ..\..\Tools\BuildCpp.cmd %target% %model% .\newDoubleToStringLab.exe .\newDoubleToStringLab.cpp || GOTO error
.\newDoubleToStringLab.exe || GOTO error

ECHO Success!
POPD
EXIT /b 0

:error
ECHO Error %ERRORLEVEL%
POPD
EXIT /b %ERRORLEVEL%
