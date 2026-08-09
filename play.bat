@echo off
REM Launch the game. With no arguments you get the main menu; anything you pass
REM here is forwarded, e.g.  play.bat --host --bots 5
setlocal
cd /d "%~dp0"
bin\gunlife.exe %*
