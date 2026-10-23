@echo off
rem Compila o ambiente de testes (MSVC) como DLL e roda pelo run.py. Quadros PNG em tools\sim\out.
rem DLL com nome novo a cada build: o Smart App Control do Windows barra .exe novos sem assinatura
rem e as vezes um binario recompilado; a DLL carregada pelo Python (assinado) costuma passar.
setlocal
set "ROOT=%~dp0..\.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio com C++ nao encontrado & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
del /q "%~dp0build\roadpanel_sim*.*" 2>nul
set "DLL=%~dp0build\roadpanel_sim_%RANDOM%%RANDOM%.dll"
cl /nologo /LD /std:c++17 /O2 /EHsc /utf-8 /bigobj /W1 /D_USE_MATH_DEFINES /FI"%~dp0msvc_compat.h" ^
  /Fo"%~dp0build\\" /Fe"%DLL%" ^
  "%~dp0sim_main.cpp" ^
  "%ROOT%\src\app\App.cpp" ^
  "%ROOT%\src\render\Canvas.cpp" "%ROOT%\src\render\Hud.cpp" "%ROOT%\src\render\Logo.cpp" ^
  "%ROOT%\src\render\Scene.cpp" ^
  "%ROOT%\src\track\Geometry.cpp" "%ROOT%\src\track\Localizer.cpp" "%ROOT%\src\track\Network.cpp" ^
  "%ROOT%\src\track\Planner.cpp" >"%~dp0build\cl.log" || (type "%~dp0build\cl.log" & exit /b 1)
python "%~dp0run.py" "%DLL%" "%~dp0out"
