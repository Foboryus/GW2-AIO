@echo off
echo ================================================
echo  GW2 AIO Manager - Portable Package Builder
echo ================================================
echo.

set BUILD_DIR=build\Release
set OUTPUT_DIR=dist\portable

if not exist %BUILD_DIR%\GW2AIO.exe (
    echo ERROR: Build not found. Run CMake build first.
    exit /b 1
)

echo Creating portable package...

:: Create output directory
if exist %OUTPUT_DIR% rmdir /s /q %OUTPUT_DIR%
mkdir %OUTPUT_DIR%

:: Copy main executable
copy %BUILD_DIR%\GW2AIO.exe %OUTPUT_DIR%\

:: Copy helper DLL (for GPU detection and window positioning)
copy %BUILD_DIR%\GW2AIOHelper.dll %OUTPUT_DIR%\

:: Copy Qt DLLs
copy %BUILD_DIR%\Qt6Core.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6Gui.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6Widgets.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6Network.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6Qml.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6Quick.dll %OUTPUT_DIR%\
copy %BUILD_DIR%\Qt6OpenGL.dll %OUTPUT_DIR%\

:: Copy Qt plugins
mkdir %OUTPUT_DIR%\platforms
copy %BUILD_DIR%\platforms\qwindows.dll %OUTPUT_DIR%\platforms\

mkdir %OUTPUT_DIR%\styles
copy %BUILD_DIR%\styles\qwindowsvistastyle.dll %OUTPUT_DIR%\styles\

mkdir %OUTPUT_DIR%\imageformats
copy %BUILD_DIR%\imageformats\*.dll %OUTPUT_DIR%\imageformats\

:: Copy resources
xcopy /s /e /i resources %OUTPUT_DIR%\resources

:: Create portable marker file
echo This file enables portable mode. > %OUTPUT_DIR%\portable.txt
echo Data will be stored in the 'data' folder next to the executable. >> %OUTPUT_DIR%\portable.txt

:: Create empty data directories
mkdir %OUTPUT_DIR%\data
mkdir %OUTPUT_DIR%\data\logs
mkdir %OUTPUT_DIR%\data\MarkerPacks
mkdir %OUTPUT_DIR%\data\BlishModules
mkdir %OUTPUT_DIR%\data\RadialMenus

:: Create README
echo GW2 AIO Manager - Portable Edition > %OUTPUT_DIR%\README.txt
echo ================================== >> %OUTPUT_DIR%\README.txt
echo. >> %OUTPUT_DIR%\README.txt
echo This is the portable version. All data is stored in the 'data' folder. >> %OUTPUT_DIR%\README.txt
echo. >> %OUTPUT_DIR%\README.txt
echo To install marker packs, place .taco files in data\MarkerPacks >> %OUTPUT_DIR%\README.txt
echo To install Blish modules, place .bhm files in data\BlishModules >> %OUTPUT_DIR%\README.txt
echo. >> %OUTPUT_DIR%\README.txt
echo For the installed version, use the installer instead. >> %OUTPUT_DIR%\README.txt

echo.
echo Portable package created in: %OUTPUT_DIR%
echo.

:: Create ZIP
echo Creating ZIP archive...
powershell -Command "Compress-Archive -Path '%OUTPUT_DIR%\*' -DestinationPath 'dist\GW2AIO_Portable.zip' -Force"

echo Done!
echo ZIP created: dist\GW2AIO_Portable.zip
