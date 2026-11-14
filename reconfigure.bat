@echo off
setlocal

REM Execute sub-script and return control here
call C:\Tools\Espressif\esp-idf\export.bat

rmdir /s /q build
rmdir /s /q managed_components
idf.py reconfigure
pause
exit
