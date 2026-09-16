@echo off
setlocal
echo ========================================================
echo         OpenRipper Vulkan Model & Geometry Dumper      
echo ========================================================
echo.

if "%~1"=="" (
    echo Usage: Drag and drop any Vulkan game .exe onto this .bat file!
    echo Or pass the game path as an argument.
    echo.
    set /p GAME_PATH="Enter game .exe path: "
) else (
    set "GAME_PATH=%~1"
)

if not exist "%GAME_PATH%" (
    echo [ERROR] Game executable not found: %GAME_PATH%
    pause
    exit /b 1
)

if exist "%~dp0build\VkLayer_openripper.json" (
    set "LAYER_DIR=%~dp0build"
) else (
    set "LAYER_DIR=%~dp0"
)
set "VK_LAYER_PATH=%LAYER_DIR%"
set "VK_INSTANCE_LAYERS=VK_LAYER_OPENRIPPER_capture"
set "ENABLE_OPENRIPPER=1"

echo [*] VK_LAYER_PATH set to: %VK_LAYER_PATH%
echo [*] VK_INSTANCE_LAYERS set to: %VK_INSTANCE_LAYERS%
echo [*] Output directory: C:\OpenRipperDumps
echo [*] Hotkey to rip: [ 0 ] (Обычная клавиша 0 в верхнем ряду клавиатуры)
echo [*] Alternative hotkeys: [ INSERT ], [ F8 ]
echo [*] Audio feedback: You will hear a BEEP when rip starts and completes!
echo.
echo Launching game...
cd /d "%~dp1"
start "" "%GAME_PATH%"

echo.
echo [OK] Game started! Press '0' or 'Insert' in game.
pause
