@ECHO OFF
SETLOCAL
CD /D "%~dp0"
pandoc -s -o "Makaron Documentation.html" --metadata title="Makaron Documentation" --include-in-header pandoc.css "Makaron Documentation.md" || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
