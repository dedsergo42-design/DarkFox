@echo off
rem ===========================================================================
rem Сборка лоадеров DarkFox (x64, GUI): обычного и дев-варианта.
rem
rem ВАЖНО ПРО ОКРУЖЕНИЕ: vcvars64.bat в этой установке BuildTools НЕ РАБОТАЕТ.
rem В D:\BuildTools есть VC\Tools\MSVC\14.51.36231 (cl/link + lib, всё на месте)
rem и Windows SDK 10.0.26100.0, но отсутствует D:\BuildTools\Common7\Tools\
rem vsdevcmd.bat -- его ставит компонент VS Installer'а, которого здесь нет.
rem vcvarsall.bat проверяет его первым делом и падает с
rem   "The specified configuration type is missing."
rem поэтому окружение выставляем руками: INCLUDE / LIB / PATH. Ровно то же
rem самое делает build_cl.py для самой DLL -- набор каталогов тот же.
rem
rem Запускать нужно из cmd.exe (или через python subprocess). Из PowerShell
rem этот .bat молча ничего не делает и возвращает 0.
rem
rem Только x64: cs2.exe 64-битная, 32-битный LoadLibrary туда не встанет.
rem
rem Из одного исходника получаются два EXE: имя инжектируемой DLL задаётся
rem макросом DARKFOX_DLL_NAME, дев-сборка помечается DARKFOX_DEV_BUILD
rem (янтарный акцент и бейдж DEV в шапке).
rem
rem Оба EXE встраивают loader.rc с манифестом requireAdministrator -- UAC
rem запрашивается при старте, иначе инжект в cs2.exe упирается в ACCESS_DENIED.
rem
rem Собираются ЧЕТЫРЕ EXE: GDI-версия (*.exe) и xui/D3D11-версия (*Xui*.exe).
rem GDI-версия моргала (рисование прямо в DC окна без бэкбуфера), xui рисует
rem через xdraw/D3D11 -- один Present на кадр, тиринга нет.
rem xui-сборке нужны xdraw.obj и xui.obj: собираем их здесь же с урезанным
rem PCH (loader\_xstandalone\pch\pch.hpp), потому что полный pch/pch.hpp
rem тянет phnt/zydis/poly2d/bc7 -- для лоадера это лишние зависимости.
rem
rem /utf-8 обязателен: исходники в UTF-8 БЕЗ BOM, иначе MSVC прочитает
rem кириллические литералы как CP1251 и в окне будет кракозябра.
rem ===========================================================================

setlocal enableextensions

set "ROOT=%~dp0"
set "OUT=%ROOT%..\bin"
set "PROJ=%ROOT%.."
set "XSTD=%ROOT%_xstandalone"
set "VCPKG=%PROJ%\vcpkg_installed\x64-windows-static\x64-windows-static"

rem --- тулчейн: явные пути, без vcvars ------------------------------------
rem BuildTools лежит на D:, SDK -- на C:. Оба пути переопределяются снаружи.
if not defined DARKFOX_VS_DIR  set "DARKFOX_VS_DIR=D:\BuildTools"
if not defined DARKFOX_SDK_DIR set "DARKFOX_SDK_DIR=C:\Program Files (x86)\Windows Kits\10"

set "MSVCVER=14.51.36231"
set "SDKVER=10.0.26100.0"
set "VCTOOLS=%DARKFOX_VS_DIR%\VC\Tools\MSVC\%MSVCVER%"
set "SDK=%DARKFOX_SDK_DIR%"

if not exist "%VCTOOLS%\bin\Hostx64\x64\cl.exe" (
    echo [!] cl.exe not found: "%VCTOOLS%\bin\Hostx64\x64\cl.exe"
    echo     Укажи каталог BuildTools в DARKFOX_VS_DIR.
    goto :failed
)

if not exist "%SDK%\Include\%SDKVER%\um\windows.h" (
    echo [!] Windows SDK not found: "%SDK%\Include\%SDKVER%"
    echo     Укажи каталог Windows Kits\10 в DARKFOX_SDK_DIR.
    goto :failed
)

rem RCDIR/RCBIN -- с завершающим слэшем НЕТ, иначе при подстановке в "/i %RCDIR%"
rem закрывающая кавычка экранируется и rc видит битую командную строку (RC1107).
rem %~dp0 всегда даёт хвостовой '\', поэтому срезаем его для путей в кавычках.
set "SRCDIR=%ROOT:~0,-1%"
set "RCDIR=%SDK%\bin\%SDKVER%\x64"
set "RCTOOL=%RCDIR%\rc.exe"

set "INCLUDE=%PROJ%\external\phnt;%PROJ%;%VCPKG%\include;%VCTOOLS%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt;%SDK%\Include\%SDKVER%\cppwinrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64;%VCPKG%\lib"
set "PATH=%VCTOOLS%\bin\Hostx64\x64;%RCDIR%;%PATH%"

rem rc.exe не читает INCLUDE из окружения (у него свой /i) -- пути к заголовкам
rem ему передаём явно, иначе на #include <windows.h> падает RC1015.
set "RCINC=/i "%VCTOOLS%\include" /i "%SDK%\Include\%SDKVER%\ucrt" /i "%SDK%\Include\%SDKVER%\um" /i "%SDK%\Include\%SDKVER%\shared" /i "%SRCDIR%""

if not exist "%RCTOOL%" (
    echo [!] rc.exe not found: "%RCTOOL%"
    goto :failed
)

if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"

rem ---------------------------------------------------------------------------
rem GDI-версия
rem ---------------------------------------------------------------------------
rem /MANIFEST:NO -- отключаем АВТОманифест линкера и подсовываем свой через
rem ресурс loader.rc. Если этого не сделать, линкер вставит дефолтный
rem манифест без requestedExecutionLevel=requireAdministrator, и лоадер
rem стартует без UAC -- инжект упадёт с ACCESS_DENIED.
rem _UNICODE/DUNICODE обязательны: интерфейс полностью на широких строках.
set "FLAGS=/nologo /std:c++17 /EHsc /O2 /MT /W3 /utf-8 /DUNICODE /D_UNICODE"
set "LIBS=user32.lib gdi32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib dwmapi.lib advapi32.lib"

echo  - loader.rc / loader.cpp (release)
"%RCTOOL%" /nologo %RCINC% /fo loader.res "%SRCDIR%\loader.rc"
if errorlevel 1 goto :failed

cl %FLAGS% /Fe:DarkFoxLoader.exe "%SRCDIR%\loader.cpp" loader.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO %LIBS%
if errorlevel 1 goto :failed

echo  - loader.rc / loader.cpp (dev)
"%RCTOOL%" /nologo %RCINC% /DDARKFOX_DEV_ICON /fo loader_dev.res "%SRCDIR%\loader.rc"
if errorlevel 1 goto :failed

cl %FLAGS% /DDARKFOX_DEV_BUILD /DDARKFOX_DLL_NAME=DarkFox-dev ^
   /Fe:DarkFoxLoaderDev.exe "%SRCDIR%\loader.cpp" loader_dev.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO %LIBS%
if errorlevel 1 goto :failed

rem ---------------------------------------------------------------------------
rem xui-версия: xdraw.obj + xui.obj + loader_xui.obj
rem ---------------------------------------------------------------------------
rem include: vcpkg (freetype) + stub-pch. Порядок важен: _xstandalone идёт
rem раньше корня проекта, иначе <pch/pch.hpp> найдётся в проекте и притащит
rem phnt/zydis.
set "XFLAGS=/nologo /std:c++20 /EHsc /O2 /MT /W3 /DNOMINMAX /utf-8 /DUNICODE /D_UNICODE"
set "XINC=/I"%PROJ%"/external"
set "XINC=%XINC% /I"%VCPKG%\include" /I"%XSTD%" /I"%PROJ%""
set "XLIBS=d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib shlwapi.lib shell32.lib ole32.lib advapi32.lib user32.lib gdi32.lib freetype.lib"

echo  - xdraw.cpp
cl %XFLAGS% %XINC% /c "%PROJ%\external\xdraw\xdraw.cpp" /Fo:_xdraw.obj
if errorlevel 1 goto :failed

echo  - xui.cpp
cl %XFLAGS% %XINC% /c "%PROJ%\external\xdraw\xui\xui.cpp" /Fo:_xui.obj
if errorlevel 1 goto :failed

rem Дев-вариант компилируется ОТДЕЛЬНО: имя инжектируемой DLL (DarkFox-dev.dll)
rem и бейдж DEV зашиваются макросами на этапе компиляции, а не линковки --
rem поэтому одного loader_xui.obj на два EXE не хватает.
echo  - loader_xui.cpp (release)
cl %XFLAGS% %XINC% /c "%SRCDIR%\loader_xui.cpp" /Fo:_lx.obj
if errorlevel 1 goto :failed

echo  - loader_xui.cpp (dev)
cl %XFLAGS% /DDARKFOX_DEV_BUILD /DDARKFOX_DLL_NAME=DarkFox-dev ^
   %XINC% /c "%SRCDIR%\loader_xui.cpp" /Fo:_lxd.obj
if errorlevel 1 goto :failed

echo  - link DarkFoxLoaderXui.exe
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:DarkFoxLoaderXui.exe ^
   _lx.obj _xdraw.obj _xui.obj loader.res /LIBPATH:"%VCPKG%\lib" %XLIBS%
if errorlevel 1 goto :failed

echo  - link DarkFoxLoaderXuiDev.exe
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:DarkFoxLoaderXuiDev.exe ^
   _lxd.obj _xdraw.obj _xui.obj loader_dev.res /LIBPATH:"%VCPKG%\lib" %XLIBS%
if errorlevel 1 goto :failed

del /q _lx.obj _lxd.obj _xdraw.obj _xui.obj >nul 2>&1

popd

echo.
echo built "%OUT%\DarkFoxLoader.exe"       and "%OUT%\DarkFoxLoaderDev.exe"
echo built "%OUT%\DarkFoxLoaderXui.exe"    and "%OUT%\DarkFoxLoaderXuiDev.exe"
endlocal
exit /b 0

:failed
popd 2>nul
echo.
echo build failed
endlocal
exit /b 1
