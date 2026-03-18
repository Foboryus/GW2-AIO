cmake_minimum_required(VERSION 3.20)
project(GW2AIO VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Generate compile_commands.json for IDE include-path resolution (eliminates false positives)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui
    Widgets 
    Qml 
    Quick 
    Network
    OpenGL
    OpenGLWidgets
    Xml
    Svg
)

# QuaZip (ZIP archive handling via vcpkg)
find_package(QuaZip-Qt6 REQUIRED)

# Source files
set(CORE_SOURCES
    src/core/GW2Detector.cpp
    src/core/AddonManager.cpp
    src/core/UpdateChecker.cpp
    src/core/LaunchManager.cpp
    src/core/BlishModuleManager.cpp
    src/core/MumbleLink.cpp
    src/core/GW2WindowWatcher.cpp
    src/core/ProfileManager.cpp
    src/core/LocalDatManager.cpp
    src/core/MutexManager.cpp
    src/core/ServerManager.cpp
    src/core/GW2APIClient.cpp
    src/core/SelfUpdater.cpp
    src/core/SettingsManager.cpp
    src/core/Logger.cpp
    src/core/Localization.cpp
    src/core/KeybindConflictDetector.cpp
    src/core/ZipExtractor.cpp
    src/core/ThemeManager.cpp
    src/core/ThemeData.cpp
    src/core/UpdateManager.cpp
    src/core/GFXManager.cpp
    src/core/DataService.cpp
    src/core/DataIntegrityChecker.cpp
    src/core/HotkeyManager.cpp
    src/core/LocalStorageBackend.cpp
    src/core/AtomicFileWriter.cpp
    src/core/CefManager.cpp
    src/core/CredentialRefreshManager.cpp
    src/core/DllInjector.cpp
)

set(CORE_HEADERS
    src/core/GW2Detector.h
    src/core/AddonManager.h
    src/core/UpdateChecker.h
    src/core/LaunchManager.h
    src/core/MutexManager.h
    src/core/ProfileManager.h
    src/core/BlishModuleManager.h
    src/core/MumbleLink.h
    src/core/GitHubReleaseChecker.h
    src/core/SelfUpdater.h
    src/core/SettingsManager.h
    src/core/Logger.h
    src/core/CredentialManager.h
    src/core/Result.h
    src/core/CrashHandler.h
    src/core/AppConfig.h
    src/core/CommandLineParser.h
    src/core/ZipExtractor.h
    src/core/GW2APIClient.h
    src/core/ThemeManager.h
    src/core/ThemeData.h
    src/core/Localization.h
    src/core/DiscordRPC.h
    src/core/AudioManager.h
    src/core/KeybindConflictDetector.h
    src/core/BackupManager.h
    src/core/DllInjector.h
    src/core/LocalDatManager.h
    src/core/ServerManager.h
    src/core/GW2WindowWatcher.h
    src/core/GFXManager.h
    src/core/DataService.h
    src/core/DataIntegrityChecker.h
    src/core/HotkeyManager.h
    src/core/StorageBackend.h
    src/core/LocalStorageBackend.h
    src/core/AtomicFileWriter.h
    src/core/CredentialRefreshManager.h
    # QSS template sections (header-only, for IDE visibility)
    src/core/qss/01_Base.h
    src/core/qss/02_Navigation.h
    src/core/qss/03_FormControls.h
    src/core/qss/04_DataWidgets.h
    src/core/qss/05_Buttons.h
    src/core/qss/06_WindowChrome.h
    src/core/qss/07_Labels.h
    src/core/qss/08_Containers.h
    src/core/qss/09_Misc.h
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/OverlayWindow.cpp
    src/ui/OverlayMenuWidget.cpp
    src/ui/ExclusionZoneEditor.cpp
    src/ui/WindowGridSelector.cpp
    src/ui/PlatformLaunchDialog.cpp
    src/ui/LauncherWidget.cpp
    src/ui/ProfileEditor.cpp
    src/ui/NetworkWidget.cpp
    src/ui/SettingsPage.cpp
    src/ui/SetupWizard.cpp
    src/ui/SettingsWidget.cpp
    src/ui/MarkerPackBrowser.cpp
    src/ui/HotkeyEditor.cpp
    src/ui/ToggleSwitch.cpp
    src/ui/StyledTooltip.cpp

    src/ui/GridCell.cpp
    # Profile tab widgets
    src/ui/profile/ArgumentsTabWidget.cpp
    src/ui/profile/AddonsTabWidget.cpp
    src/ui/profile/WindowTabWidget.cpp
    src/ui/profile/GraphicsTabWidget.cpp
    src/ui/profile/LoginTabWidget.cpp
    src/ui/profile/NetworkTabWidget.cpp
    src/ui/profile/GeneralTabWidget.cpp
    src/ui/profile/HotkeysTabWidget.cpp
    src/ui/profile/MarkersTabWidget.cpp
    src/ui/profile/MarkerSettingsWidget.cpp
    src/ui/profile/MarkerPackCard.cpp
)

set(UI_HEADERS
    src/ui/MainWindow.h
    src/ui/OverlayWindow.h
    src/ui/OverlayMenuWidget.h
    src/ui/ExclusionZoneEditor.h

    src/ui/ErrorDialog.h
    src/ui/HotkeyEditor.h
    src/ui/FontRenderer.h
    src/ui/SettingsPage.h
    src/ui/SetupWizard.h
    src/ui/MarkerPackBrowser.h
    src/ui/AboutDialog.h
    src/ui/SplashScreen.h
    src/ui/LauncherWidget.h
    src/ui/ProfileEditor.h
    src/ui/ToggleSwitch.h
    src/ui/SettingsWidget.h
    src/ui/NetworkWidget.h
    src/ui/PlatformLaunchDialog.h
    src/ui/profile/HotkeysTabWidget.h
    src/ui/profile/MarkersTabWidget.h
    src/ui/profile/MarkerSettingsWidget.h
    src/ui/profile/MarkerPackCard.h
)

set(MODEL_HEADERS
    src/models/Addon.h
    src/models/LaunchProfile.h
    src/models/BlishModule.h
)

# Feature: Radial Menus
set(RADIAL_HEADERS
    src/features/radial/RadialMenu.h
    src/features/radial/RadialConfig.h
    src/features/radial/RadialEngine.h
    src/features/radial/RadialWidget.h
    src/features/radial/RadialController.h
)

# Radial implementation files
set(RADIAL_SOURCES
    src/features/radial/RadialEngine.cpp
    src/features/radial/RadialWidget.cpp
    src/features/radial/RadialConfig.cpp
    src/features/radial/RadialController.cpp
)

# Feature: DPS Tracker
set(DPS_HEADERS
    src/features/dps/CombatModels.h
    src/features/dps/CombatTracker.h
    src/features/dps/DPSWidget.h
    src/features/dps/DPSController.h
    src/features/dps/ArcDPSModels.h
    src/features/dps/EVTCParser.h
    src/features/dps/ArcDPSWatcher.h
    src/features/dps/ArcDPSRealTime.h
)

# DPS implementation files
set(DPS_SOURCES
    src/features/dps/EVTCParser.cpp
    src/features/dps/CombatTracker.cpp
    src/features/dps/ArcDPSRealTime.cpp
    src/features/dps/ArcDPSWatcher.cpp
    src/features/dps/DPSController.cpp
)

# Feature: Marker System (TacO compatible)
set(MARKER_HEADERS
    src/features/markers/MarkerModels.h
    src/features/markers/TacoParser.h
    src/features/markers/MarkerManager.h
    src/features/markers/MarkerRenderer.h
    src/features/markers/MarkerController.h
    src/features/markers/GLMarkerRenderer.h
    src/features/markers/TextureCache.h
    src/features/markers/ActivationStore.h
    src/features/markers/MinimapRenderer.h
    src/features/markers/ImageCache.h
    src/features/markers/Gw2ApiClient.h
    src/features/markers/MarkerSettingsManager.h
    src/features/markers/MarkerPackRegistry.h
)

# Marker implementation files
set(MARKER_SOURCES
    src/features/markers/TacoParser.cpp
    src/features/markers/GLMarkerRenderer.cpp
    src/features/markers/MarkerRenderer.cpp
    src/features/markers/MarkerManager.cpp
    src/features/markers/MarkerController.cpp
    src/features/markers/TextureCache.cpp
    src/features/markers/ActivationStore.cpp
    src/features/markers/MinimapRenderer.cpp
    src/features/markers/ImageCache.cpp
    src/features/markers/Gw2ApiClient.cpp
    src/features/markers/MarkerSettingsManager.cpp
    src/features/markers/MarkerPackRegistry.cpp
)

# Feature: .NET Module Host (Blish-HUD)
set(MODULE_HEADERS
    src/features/modules/BlishModels.h
    src/features/modules/NetHost.h
    src/features/modules/ModuleLoader.h
    src/features/modules/ModuleController.h
    src/features/modules/BlishGraphicsBridge.h
    src/features/modules/BlishAPIServices.h
)

# Module implementation files
set(MODULE_SOURCES
    src/features/modules/BlishGraphicsBridge.cpp
    src/features/modules/BlishAPIServices.cpp
    src/features/modules/ModuleLoader.cpp
    src/features/modules/ModuleController.cpp
)

# D3D11 Overlay Rendering Engine
set(RENDERING_HEADERS
    src/rendering/D3D11Context.h
    src/rendering/D3D11OverlayWindow.h
    src/rendering/ExclusionData.h
    src/rendering/GlyphAtlas.h
    src/rendering/SpriteBatch.h
    src/rendering/MarkerPipeline.h
    src/rendering/TrailPipeline.h
)

set(RENDERING_SOURCES
    src/rendering/D3D11Context.cpp
    src/rendering/D3D11OverlayWindow.cpp
    src/rendering/DebugOverlayWidget.cpp
    src/rendering/GlyphAtlas.cpp
    src/rendering/SpriteBatch.cpp
    src/rendering/MarkerPipeline.cpp
    src/rendering/TrailPipeline.cpp
)

# Generate Windows resource file with correct icon path
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
        @ONLY
    )
endif()

# Main application executable (lives in lib/ subfolder, launched by stub)
add_executable(GW2AIO_app WIN32
    src/main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${UI_SOURCES}
    ${UI_HEADERS}
    ${MODEL_HEADERS}
    ${RADIAL_HEADERS}
    ${RADIAL_SOURCES}
    ${DPS_HEADERS}
    ${DPS_SOURCES}
    ${MARKER_HEADERS}
    ${MARKER_SOURCES}
    ${MODULE_HEADERS}
    ${MODULE_SOURCES}
    ${RENDERING_HEADERS}
    ${RENDERING_SOURCES}
    resources/qml.qrc
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_BINARY_DIR}/app.rc>  # Generated Windows icon resource
)

# Require admin elevation on Windows (for multibox mutex closing)
if(WIN32 AND MSVC)
    set_target_properties(GW2AIO_app PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# Output main app to lib/ subfolder (DLLs are here too)
# Using _RELEASE suffix to avoid MSVC multi-config adding extra Release/ subdirectory
set_target_properties(GW2AIO_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
)

target_include_directories(GW2AIO_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(GW2AIO_app PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Xml
    Qt6::Svg
    QuaZip::QuaZip
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    dcomp
)

# Copy Qt DLLs to lib/ subdirectory (alongside main app exe)
if(WIN32)
    add_custom_command(TARGET GW2AIO_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:GW2AIO_app>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE:Qt6::Widgets>
            $<TARGET_FILE:Qt6::Qml>
            $<TARGET_FILE:Qt6::Quick>
            $<TARGET_FILE:Qt6::Network>
            $<TARGET_FILE:QuaZip::QuaZip>
            $<TARGET_FILE_DIR:GW2AIO_app>
    )
    # QuaZip runtime dependencies (zlib, Qt6Core5Compat)
    find_file(ZLIB_DLL NAMES zlib1.dll PATHS "${CMAKE_PREFIX_PATH}/bin" NO_DEFAULT_PATH)
    find_file(QT6CORE5COMPAT_DLL NAMES Qt6Core5Compat.dll PATHS "${CMAKE_PREFIX_PATH}/bin" NO_DEFAULT_PATH)
    if(ZLIB_DLL AND QT6CORE5COMPAT_DLL)
        add_custom_command(TARGET GW2AIO_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ZLIB_DLL}"
                "${QT6CORE5COMPAT_DLL}"
                $<TARGET_FILE_DIR:GW2AIO_app>
        )
    endif()
    # Generate qt.conf next to GW2AIO_app.exe (Qt reads it from applicationDirPath)
    # Prefix/Plugins = "." means plugins are in lib/ subdirectories (platforms/, iconengines/, tls/)
    file(WRITE "${CMAKE_BINARY_DIR}/qt.conf" "[Paths]\nPrefix = .\nPlugins = .\n")
    add_custom_command(TARGET GW2AIO_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/qt.conf"
            $<TARGET_FILE_DIR:GW2AIO_app>/qt.conf
    )
    # Copy ALL vcpkg runtime DLLs to lib/ (transitive deps windeployqt misses:
    # brotli, icu, harfbuzz, freetype, pcre2, zstd, double-conversion, etc.)
    file(GLOB VCPKG_RUNTIME_DLLS "${CMAKE_PREFIX_PATH}/bin/*.dll")
    add_custom_command(TARGET GW2AIO_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VCPKG_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:GW2AIO_app>
    )
endif()

# Build helper DLL for game load detection
add_subdirectory(src/helper)

# Build launcher stub (pure Win32, no Qt)
add_subdirectory(src/launcher)
