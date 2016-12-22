@REM Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.

@echo OFF

REM Build tags file
ctags -R

REM Check if build tool is on path
REM >nul, 2>nul will remove the output text from the where command
where cl.exe >nul 2>nul
if %errorlevel%==1 call msvc86.bat

REM Drop compilation files into build folder
IF NOT EXIST ..\bin mkdir ..\bin
pushd ..\bin

set GL3W=..\libs\gl3w
set GLFW=..\libs\glfw
set IMGUI=..\libs\imgui

REM EHa- disable exception handling (we don't use)
REM GR- disable c runtime type information (we don't use)

REM MD use dynamic runtime library
REM MT use static runtime library, so build and link it into exe

REM Oi enable intrinsics optimisation, let us use CPU intrinsics if there is one
REM instead of generating a call to external library (i.e. CRT).

REM Zi enables debug data, Z7 combines the debug files into one.

REM W4 warning level 4
REM WX treat warnings as errors
REM wd4100 ignore unused argument parameters

set compileFlags=-EHa- -GR- -Oi -MT -Z7 -W4 -wd4100

REM Include directories
set includeFlags=/I %GL3W% /I %GLFW%\include /I %IMGUI%

REM Link libraries
set linkLibraries=%GLFW%\vc2015-x86-static\lib\glfw3.lib opengl32.lib gdi32.lib shell32.lib
set compileFiles= ..\src\*.cpp %IMGUI%\imgui*.cpp %GL3W%\GL\gl3w.c

cl %compileFlags% %compileFiles% %includeFlags% /link -subsystem:console %linkLibraries% /OUT:"winjump.exe"

popd
