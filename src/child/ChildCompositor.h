#pragma once

/**
 * @file ChildCompositor.h
 * @brief Compositor child process — single overlay window per GW2 instance
 *
 * The ChildCompositor owns the ONE overlay window that sits above GW2.
 * It samples SharedTexture layers from sibling children (3D, minimap,
 * radial, HUD) and composites them into a single D3D11 output via
 * a fullscreen quad shader.
 *
 * This eliminates z-order competition between multiple overlay windows
 * and reduces DWM recomposition to a single SetWindowPos call.
 *
 * Architecture:
 * - One compositor per running profile (GW2 instance)
 * - Spawned FIRST by ChildProcessManager, before feature children
 * - Receives INTERACTIVE_RECTS from feature children → hybrid click-through
 * - Forwards MOUSE_DOWN/UP/MOVE to feature children for interactive areas
 * - Sends COMPOSITOR_READY + RESIZE to grandfather for relay
 *
 * DO NOT ADD:
 * - Feature-specific rendering (markers, trails, radial — belongs in children)
 * - Inline implementations beyond trivial getters (use .cpp)
 */

#include "ChildProcess.h"

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11_1.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <QHash>
#include <QList>
#include <QRect>
#include <QTimer>

using Microsoft::WRL::ComPtr;

class D3D11Context;
class SharedTextureConsumer;

/**
 * @brief Info about one interactive region reported by a feature child
 */
struct InteractiveRect {
  QRect rect;       // Screen-relative rect (relative to compositor window)
  QString layer;    // Feature key that owns this rect ("hud", "radial", etc.)
};

class ChildCompositor : public ChildProcess {
  Q_OBJECT

public:
  explicit ChildCompositor(const QString &profileId,
                           const QString &mumbleName,
                           qint64 gw2Pid,
                           const QString &pipeName,
                           const QString &profileName,
                           QObject *parent = nullptr);
  ~ChildCompositor() override;

protected:
  // --- ChildProcess virtual overrides ---
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;
  void onReloadPacks() override;

private:
  // --- Window management ---
  bool createCompositorWindow();
  void destroyCompositorWindow();
  bool setupDirectComposition();
  void updatePosition();

  // --- Rendering ---
  void onRenderFrame();
  void renderLayers();
  bool initializeQuadPipeline();
  void tryOpenConsumers();

  // --- GW2 window tracking ---
  void installEventHook();
  void uninstallEventHook();
  static void CALLBACK winEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                    HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD idEventThread, DWORD dwmsEventTime);
  static void CALLBACK foregroundProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                      HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD idEventThread, DWORD dwmsEventTime);

  // --- GW2 window discovery ---
  bool findGW2Window();

  // --- Process exit monitoring ---
  static void CALLBACK processExitCallback(PVOID context, BOOLEAN timedOut);
  void registerProcessExitWait();
  void unregisterProcessExitWait();

  // --- Hit testing ---
  void updateInteractiveRects(const QString &layer, const QList<QRect> &rects);
  bool isPointInteractive(int x, int y, QString &outLayer) const;

  // --- Win32 WndProc ---
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);

  // === Members ===

  // Win32 window
  HWND m_hwnd = nullptr;
  std::wstring m_windowClassName;

  // GW2 tracking
  HWND m_gw2Hwnd = nullptr;
  uint32_t m_gw2ProcessId = 0;
  bool m_gw2Focused = false;
  HWINEVENTHOOK m_eventHook = nullptr;
  HWINEVENTHOOK m_foregroundHook = nullptr;
  HANDLE m_processWaitHandle = nullptr;
  HANDLE m_gw2ProcessHandle = nullptr;

  // D3D11 rendering
  D3D11Context *m_d3dContext = nullptr;

  // DirectComposition
  ComPtr<IDCompositionDevice> m_dcompDevice;
  ComPtr<IDCompositionTarget> m_dcompTarget;
  ComPtr<IDCompositionVisual> m_dcompVisual;

  // Fullscreen quad pipeline (compositor shader)
  ComPtr<ID3D11VertexShader> m_quadVS;
  ComPtr<ID3D11PixelShader> m_quadPS;
  ComPtr<ID3D11Buffer> m_quadVB;        // unused (SV_VertexID approach)
  ComPtr<ID3D11InputLayout> m_quadLayout; // unused (SV_VertexID approach)
  ComPtr<ID3D11SamplerState> m_linearSampler;
  ComPtr<ID3D11Buffer> m_screenSizeCB;  // PS constant buffer: float2 screenSize + pad

  // Shared texture consumers (one per feature layer)
  QHash<QString, SharedTextureConsumer *> m_layers;

  // Layer draw order (bottom to top)
  QStringList m_layerOrder;

  // Interactive rects for hybrid click-through
  // layer name → list of clickable rects
  QHash<QString, QList<QRect>> m_interactiveRects;

  // Render timer
  QTimer *m_renderTimer = nullptr;

  // Content visibility (hide when GW2 not focused)
  bool m_contentVisible = false;
};
