@ECHO OFF
SETLOCAL
CD /D "%~dp0\.."
IF NOT EXIST output MKDIR output
REM Builds the libFuzzer target with MSVC: optimized with asserts on (release with NDEBUG undefined, since beta's debug
REM CRT is several times slower) and AddressSanitizer. Give libFuzzer a scratch corpus directory first, since it writes
REM new inputs there:
REM	output\MakaronFuzz.exe -dict=tests/fuzz/makaron.dict -artifact_prefix=output/ output/makaronCorpus
SET CPP_OPTIONS=/I src /fsanitize=address /fsanitize=fuzzer /U NDEBUG /Z7
CALL tools\BuildCpp.cmd release x64 output\MakaronFuzz.exe tests\MakaronFuzz.cpp src\Makaron.cpp /link /STACK:8388608 || EXIT /B 1
REM The ASan runtime is a DLL, so copy it next to the fuzzer to let it run outside a Visual Studio prompt
SET "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
SET "asanDll=VC\Tools\MSVC\**\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll"
FOR /F "usebackq tokens=*" %%a IN (`"%vswhere%" -latest -products * -find %asanDll%`) DO COPY /Y "%%a" output\ >NUL
IF NOT EXIST output\clang_rt.asan_dynamic-x86_64.dll (
	ECHO Could not find clang_rt.asan_dynamic-x86_64.dll
	EXIT /B 1
)
