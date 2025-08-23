@ECHO OFF
SETLOCAL
CD /D "%~dp0"
CALL BuildCpp.cmd release x64 makaron.exe -I ..\src MakaronCmd.cpp ..\src\Makaron.cpp || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
