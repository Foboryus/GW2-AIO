#pragma once

/**
 * @file ChildRadial.h
 * @brief Radial menu child process — D3D11 radial wheel rendering
 *
 * Owns: RadialController, D3D11Context (offscreen), SharedTextureProducer.
 * Renders radial wheel to SharedTexture for compositor consumption.
 * No OverlayWindow — hotkey polling via GetAsyncKeyState, cursor
 * position via GetCursorPos + GW2 window rect.
 *
 * Rendering pattern matches Child3DOverlay:
 *   render to intermediate RT → CopyResource → SharedTexture
 */

#include "ChildProcess.h"

// clang-format off
#include <windows.h>
#include <wrl/client.h>
// clang-format on

#include <d3d11.h>

class D3D11Context;
class RadialController;
class RadialSettingsManager;
class SharedTextureProducer;

using Microsoft::WRL::ComPtr;

class ChildRadial : public ChildProcess {
  Q_OBJECT

public:
  ChildRadial(const QString &profileId,
              const QString &mumbleName,
              qint64 gw2Pid,
              const QString &pipeName,
              const QString &profileName,
              QObject *parent = nullptr);
  ~ChildRadial() override;

protected:
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;

private:
  void onRenderTick();
  void pollGW2WindowSize();
  bool findGW2Window();
  bool createIntermediateRT(int width, int height);
  bool ensureD3D11();  ///< Lazy device init — called on first map entry or focus gain
  void teardownD3D11();  ///< Inverse of ensureD3D11 — destroys GPU resources on unfocus

  RadialController *m_controller = nullptr;
  RadialSettingsManager *m_radialSettings = nullptr;

  // --- Rendering pipeline ---
  D3D11Context *m_d3dContext = nullptr;
  SharedTextureProducer *m_sharedTexture = nullptr;

  // Intermediate render target (same pattern as Child3DOverlay)
  // Render here first, then CopyResource → SharedTexture
  ComPtr<ID3D11Texture2D> m_intermediateRT;
  ComPtr<ID3D11RenderTargetView> m_intermediateRTV;

  HWND m_gw2Hwnd = nullptr;
  int m_gw2Width = 0;
  int m_gw2Height = 0;
  bool m_d3dInitialized = false;  ///< True after ensureD3D11() succeeds
};
