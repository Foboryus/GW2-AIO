cmake_minimum_required(VERSION 3.22...4.2)

# ============================================================================
# GW2 AIO Child Process Targets
#
# Each child is a separate executable for a specific overlay feature.
# Children use NTFS hardlinks at runtime for per-profile naming
# (e.g., GW2AIO-3d.exe → GW2AIO-3d-MainAccount.exe).
# ============================================================================

# ---- Child resources (shaders + minimal icons) ----
qt6_add_resources(CHILD_QRC_SOURCES
    ${CMAKE_SOURCE_DIR}/resources/child.qrc
)

# ============================================================================
# GW2AIO-3d: 3D Overlay Child (proof of concept)
# ============================================================================
add_executable(GW2AIO-3d WIN32
    # Child framework
    ChildMain.cpp
    ChildProcess.cpp
    ChildProcess.h
    Child3DOverlay.cpp
    Child3DOverlay.h

    # Rendering pipeline (shared source files — compiled into this target)
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11Context.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/SharedTexture.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/GlyphAtlas.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/SpriteBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/MarkerPipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/TrailPipeline.cpp

    # Marker system (shared source files)
    ${CMAKE_SOURCE_DIR}/src/features/markers/TacoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerManager.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/ImageCache.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerSettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/ActivationStore.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerPackRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/Gw2ApiClient.cpp

    # Core utilities (shared source files)
    ${CMAKE_SOURCE_DIR}/src/core/MumbleLink.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AppConfig.h
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ZipExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AtomicFileWriter.cpp

    # Qt resources
    ${CHILD_QRC_SOURCES}

    # Windows app icon
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)

# Compile-time feature flag: tells ChildMain.cpp which child class to use
target_compile_definitions(GW2AIO-3d PRIVATE CHILD_FEATURE_3D)

target_include_directories(GW2AIO-3d PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(GW2AIO-3d PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
    Qt6::Svg
    QuaZip::QuaZip
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    dcomp
)

# Admin elevation manifest (child runs with same privileges as grandfather)
if(WIN32 AND MSVC)
    set_target_properties(GW2AIO-3d PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# Output to lib/ (alongside GW2AIO_app.exe and DLLs)
set_target_properties(GW2AIO-3d PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
)

# ============================================================================
# Shared source lists for reuse across child targets
# ============================================================================

# Rendering pipeline sources (D3D11 children)
set(CHILD_RENDERING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11Context.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11OverlayWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/DebugOverlayWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/GlyphAtlas.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/SpriteBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/MarkerPipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/TrailPipeline.cpp
)

# Marker system sources
set(CHILD_MARKER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/features/markers/TacoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerManager.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/ImageCache.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerSettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/ActivationStore.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/MarkerPackRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/features/markers/Gw2ApiClient.cpp
)

# Core utilities
set(CHILD_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/MumbleLink.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AppConfig.h
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ZipExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AtomicFileWriter.cpp
)

# Common child framework
set(CHILD_FRAMEWORK_SOURCES
    ChildMain.cpp
    ChildProcess.cpp
    ChildProcess.h
)

# D3D11 link libraries
set(CHILD_D3D_LIBS d3d11 dxgi d3dcompiler dwmapi dcomp)

# Qt + third-party link libraries
set(CHILD_QT_LIBS
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Svg
    QuaZip::QuaZip
)

# Admin elevation manifest
function(child_set_manifest TARGET_NAME)
    if(WIN32 AND MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES
            LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
        )
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
    )
endfunction()

# ============================================================================
# GW2AIO-minimap: Minimap Overlay Child
# ============================================================================
add_executable(GW2AIO-minimap WIN32
    ${CHILD_FRAMEWORK_SOURCES}
    ChildMinimap.cpp ChildMinimap.h

    # D3D11 rendering (Phase 5.9.1: TrailPipeline for minimap/bigmap GPU trails)
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11Context.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/SharedTexture.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/TrailPipeline.cpp

    # Marker system
    ${CHILD_MARKER_SOURCES}
    ${CHILD_CORE_SOURCES}

    # MinimapRenderer (QPainter 2D markers + trails on compass/big map)
    ${CMAKE_SOURCE_DIR}/src/features/markers/MinimapRenderer.cpp

    # Theme system (required by MinimapRenderer for border/indicator colors)
    ${CMAKE_SOURCE_DIR}/src/core/ThemeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ThemeData.cpp

    ${CHILD_QRC_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)
target_compile_definitions(GW2AIO-minimap PRIVATE CHILD_FEATURE_MINIMAP)
target_include_directories(GW2AIO-minimap PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GW2AIO-minimap PRIVATE ${CHILD_QT_LIBS} ${CHILD_D3D_LIBS})
child_set_manifest(GW2AIO-minimap)

# ============================================================================
# GW2AIO-bigmap: Big Map Overlay Child
# ============================================================================
add_executable(GW2AIO-bigmap WIN32
    ${CHILD_FRAMEWORK_SOURCES}
    ChildBigMap.cpp ChildBigMap.h
    ${CHILD_RENDERING_SOURCES}
    ${CHILD_MARKER_SOURCES}
    ${CHILD_CORE_SOURCES}
    ${CHILD_QRC_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)
target_compile_definitions(GW2AIO-bigmap PRIVATE CHILD_FEATURE_BIGMAP)
target_include_directories(GW2AIO-bigmap PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GW2AIO-bigmap PRIVATE ${CHILD_QT_LIBS} ${CHILD_D3D_LIBS})
child_set_manifest(GW2AIO-bigmap)

# ============================================================================
# GW2AIO-radial: Radial Menu Child (D3D11 radial wheel rendering)
# ============================================================================
add_executable(GW2AIO-radial WIN32
    ${CHILD_FRAMEWORK_SOURCES}
    ChildRadial.cpp ChildRadial.h
    RadialOverlayWindow.cpp RadialOverlayWindow.h
    RadialController.cpp RadialController.h
    RadialRenderer.cpp RadialRenderer.h
    RadialWheel.cpp RadialWheel.h
    RadialElement.h

    # D3D11 rendering context (shared source — device, swap chain, shaders)
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11Context.cpp

    # Shared texture (cross-process GPU texture sharing)
    ${CMAKE_SOURCE_DIR}/src/rendering/SharedTexture.cpp

    # Radial settings (per-profile configuration)
    ${CMAKE_SOURCE_DIR}/src/core/RadialSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RadialSettingsManager.cpp

    ${CHILD_CORE_SOURCES}
    ${CHILD_QRC_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)
target_compile_definitions(GW2AIO-radial PRIVATE CHILD_FEATURE_RADIAL)
target_include_directories(GW2AIO-radial PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GW2AIO-radial PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Svg
    QuaZip::QuaZip
    ${CHILD_D3D_LIBS}
)
child_set_manifest(GW2AIO-radial)

# ============================================================================
# GW2AIO-overlay: Overlay HUD Child (Qt menu, settings — no D3D11)
# ============================================================================
add_executable(GW2AIO-overlay WIN32
    ${CHILD_FRAMEWORK_SOURCES}
    ChildOverlay.cpp ChildOverlay.h
    ${CHILD_MARKER_SOURCES}
    ${CHILD_CORE_SOURCES}

    # OverlayWindow (transparent HWND tracking, z-order, click-through)
    ${CMAKE_SOURCE_DIR}/src/ui/OverlayWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/OverlayMenuWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ExclusionZoneEditor.cpp

    # MinimapRenderer (needed for OverlayWindow::updateHudVisibility linker)
    ${CMAKE_SOURCE_DIR}/src/features/markers/MinimapRenderer.cpp

    # Theme system (required by OverlayMenuWidget)
    ${CMAKE_SOURCE_DIR}/src/core/ThemeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ThemeData.cpp

    # Radial settings (for overlay menu toggle — read/write radialEnabled)
    ${CMAKE_SOURCE_DIR}/src/core/RadialSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RadialSettingsManager.cpp

    ${CHILD_QRC_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)
target_compile_definitions(GW2AIO-overlay PRIVATE CHILD_FEATURE_OVERLAY)
target_include_directories(GW2AIO-overlay PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GW2AIO-overlay PRIVATE
    ${CHILD_QT_LIBS}
    dwmapi  # For DWM window composition (OverlayWindow)
)
child_set_manifest(GW2AIO-overlay)

# ============================================================================
# GW2AIO-compositor: Compositor Child (single overlay window per GW2 instance)
# ============================================================================
add_executable(GW2AIO-compositor WIN32
    ${CHILD_FRAMEWORK_SOURCES}
    ChildCompositor.cpp ChildCompositor.h

    # D3D11 rendering context (device, swap chain, shaders)
    ${CMAKE_SOURCE_DIR}/src/rendering/D3D11Context.cpp

    # Shared texture (cross-process GPU texture sharing)
    ${CMAKE_SOURCE_DIR}/src/rendering/SharedTexture.cpp

    ${CHILD_CORE_SOURCES}
    ${CHILD_QRC_SOURCES}
    ${CMAKE_SOURCE_DIR}/resources/child.rc
)
target_compile_definitions(GW2AIO-compositor PRIVATE CHILD_FEATURE_COMPOSITOR)
target_include_directories(GW2AIO-compositor PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GW2AIO-compositor PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Svg
    QuaZip::QuaZip
    ${CHILD_D3D_LIBS}
)
child_set_manifest(GW2AIO-compositor)
