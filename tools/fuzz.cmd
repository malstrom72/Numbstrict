@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

REM Fuzzes the libFuzzer targets by hand (never on a schedule: CI runs on pushes and ad hoc). Builds each target,
REM runs it for the given number of minutes from the committed corpus with its dictionary, then minimizes the grown
REM corpus back into tests\fuzz, ready to commit if git shows it changed. A finding stops the script with libFuzzer's
REM exit code and leaves the input in output\fuzz (crash-*, timeout-* or oom-*) with the log next to it.
REM	tools\fuzz.cmd [minutes] [numbstrict^|makaron^|realConversion ...]		default: 10 minutes, all three targets

SET "minutes=%~1"
IF "%minutes%"=="" SET "minutes=10"
SET /A seconds=minutes*60
SET "targets="
:collect
SHIFT
IF NOT "%~1"=="" (
	SET "targets=!targets! %~1"
	GOTO collect
)
IF "%targets%"=="" SET "targets=numbstrict makaron realConversion"
IF EXIST output\fuzz RMDIR /S /Q output\fuzz
MKDIR output\fuzz\merged || GOTO error

FOR %%t IN (%targets%) DO (
	CALL :fuzzOne %%t || GOTO error
)
ECHO Fuzzing completed; commit the corpora in tests\fuzz if git shows them changed
EXIT /b 0

:fuzzOne
SET "target=%~1"
SET "script="
IF "%target%"=="numbstrict" (
	SET "script=build_numbstrict_fuzz"
	SET "exe=NumbstrictFuzz"
	SET "options=-dict=tests/fuzz/numbstrict.dict"
)
IF "%target%"=="makaron" (
	SET "script=build_makaron_fuzz"
	SET "exe=MakaronFuzz"
	SET "options=-dict=tests/fuzz/makaron.dict tests/fuzz/makaronCrashes"
)
IF "%target%"=="realConversion" (
	SET "script=build_realconversion_fuzz"
	SET "exe=RealConversionFuzz"
	SET "options=-max_len=1200"
)
IF "%script%"=="" (
	ECHO Unknown target: %target%
	EXIT /b 1
)
CALL tools\%script%.cmd || EXIT /b 1
tar -xzf tests\fuzz\%target%Corpus.tar.gz -C output\fuzz || EXIT /b 1
FOR /F %%n IN ('DIR /b output\fuzz\%target% ^| FIND /c /v ""') DO SET "before=%%n"
ECHO Fuzzing %exe% for %minutes% minutes from %before% inputs
output\%exe%.exe -max_total_time=%seconds% -artifact_prefix=output/fuzz/ output/fuzz/%target% %options% ^
		> output\fuzz\%target%.log 2>&1
IF ERRORLEVEL 1 (
	ECHO %exe% found something: see output\fuzz\%target%.log and the input in output\fuzz
	EXIT /b 1
)
MKDIR output\fuzz\merged\%target% || EXIT /b 1
output\%exe%.exe -merge=1 output/fuzz/merged/%target% output/fuzz/%target% ^
		> output\fuzz\%target%.merge.log 2>&1 || EXIT /b 1
tar -czf tests\fuzz\%target%Corpus.tar.gz -C output\fuzz\merged %target% || EXIT /b 1
FOR /F %%n IN ('DIR /b output\fuzz\merged\%target% ^| FIND /c /v ""') DO SET "after=%%n"
ECHO %exe%: no finding, corpus %before% -^> %after% inputs, tests\fuzz\%target%Corpus.tar.gz updated
EXIT /b 0

:error
EXIT /b %ERRORLEVEL%
