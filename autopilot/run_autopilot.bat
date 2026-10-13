@echo off
rem The autopilot on its own: run_autopilot.bat [indy500^|stcc^|srallyc^|daytona] [run_vjoy.py options]
rem Indy 500: the learnt network from the race start, in lockstep (it wins); the others: the optimised line or the pursuit.
cd /d "%~dp0"
set GAME=%1
if "%GAME%"=="" set GAME=indy500
shift
set POLICY=pursuit:2.5
set EXTRA=
if "%GAME%"=="indy500" set POLICY=q:runs/q_win1.pt
if "%GAME%"=="indy500" if not "%1"=="--bff" set EXTRA=--state states/indy_oval_mt.state --sync
if "%GAME%"=="stcc" set POLICY=line:runs/opt_stcc/best.json
python run_vjoy.py --game %GAME% --policy %POLICY% %EXTRA% %1 %2 %3 %4 %5 %6
