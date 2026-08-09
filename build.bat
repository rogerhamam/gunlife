@echo off
REM Configure + build Kaj's Shooter Game 3D. Output: bin\kaj_shooter.exe
setlocal
cd /d "%~dp0"

if not exist build (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || goto :fail
)
cmake --build build || goto :fail

echo.
echo Built bin\kaj_shooter.exe
goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1
