@echo off
REM Configure + build Gunlife. Output: bin\gunlife.exe
setlocal
cd /d "%~dp0"

if not exist build (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail
)
cmake --build build || goto :fail

echo.
echo Built bin\gunlife.exe
goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1
