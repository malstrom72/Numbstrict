@ECHO OFF
CD /D "%~dp0\.."
IF NOT EXIST output MKDIR output
SET CPP_OPTIONS=/fsanitize=address /fsanitize=fuzzer
CALL tools\BuildCpp.cmd beta x64 output\NumbstrictFuzz.exe tests\NumbstrictFuzz.cpp src\Numbstrict.cpp || GOTO error
REM The ASan runtime is a DLL, so copy it next to the fuzzer to let it run outside a Visual Studio prompt
SET "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
SET "asanDll=VC\Tools\MSVC\**\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll"
FOR /F "usebackq tokens=*" %%a IN (`"%vswhere%" -latest -products * -find %asanDll%`) DO COPY /Y "%%a" output\ >NUL
IF NOT EXIST output\clang_rt.asan_dynamic-x86_64.dll (
	ECHO Could not find clang_rt.asan_dynamic-x86_64.dll
	EXIT /b 1
)
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
