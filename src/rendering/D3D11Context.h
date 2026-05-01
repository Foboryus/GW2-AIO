#pragma once

/**
 * @brief D3D11 device, swap chain, and render state management
 *
 * Owns the core D3D11 resources needed for overlay rendering:
 * - Device + device context
 * - Swap chain targeting a native Win32 HWND
 * - Render target view
 * - Blend states for alpha compositing
 * - Shader compilation helpers
 *
 * Consumers:
 * - D3D11OverlayWindow: creates and owns this context
 * - RadialOverlayWindow: creates and owns this context
 * - MarkerPipeline, TrailPipeline, SpriteBatch: use device for rendering
 *
 * DO NOT ADD:
 * - Inline implementations (use D3D11Context.cpp)
 * - Rendering logic (belongs in pipeline classes)
 * - Window management (belongs in D3D11OverlayWindow)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <QSize>
#include <QString>

using Microsoft::WRL::ComPtr;

class D3D11Context {
public:
  D3D11Context();
  ~D3D11Context();

  // Non-copyable, non-movable (owns GPU resources)
  D3D11Context(const D3D11Context &) = delete;
  D3D11Context &operator=(const D3D11Context &) = delete;

  /**
   * @brief Initialize D3D11 device and swap chain for a window
   * @param hwnd Target window handle
   * @param size Initial window size
   * @return true if initialization succeeded
   */
  bool initialize(HWND hwnd, const QSize &size);

  /**
   * @brief Initialize D3D11 device only (no window, no swap chain)
   *
   * Used by feature children that render to SharedTexture instead of
   * a window. The RTV is set externally via setExternalRTV().
   * beginFrame() clears the external RTV; endFrame() flushes (no Present).
   *
   * @param size Initial render target size (for viewport)
   * @return true if device creation succeeded
   */
  bool initializeOffscreen(const QSize &size);

  /**
   * @brief Shutdown and release all D3D11 resources
   */
  void shutdown();

  /**
   * @brief Resize swap chain buffers (call on window resize)
   * @param size New window size
   */
  void resize(const QSize &size);

  /**
   * @brief Begin a frame — clear render target to transparent black
   */
  void beginFrame();

  /**
   * @brief End a frame — present the swap chain (windowed) or flush (offscreen)
   */
  void endFrame();

  /**
   * @brief Set an external RTV for offscreen rendering
   *
   * Replaces the swap chain back-buffer RTV. Used by SharedTexture
   * workflow: feature children acquire the shared texture, set its
   * RTV here, render, then release.
   *
   * @param rtv External render target view (owned by caller)
   */
  void setExternalRTV(ID3D11RenderTargetView *rtv);

  // --- Accessors ---

  ID3D11Device *device() const { return m_device.Get(); }
  ID3D11DeviceContext *context() const { return m_context.Get(); }
  ID3D11RenderTargetView *renderTargetView() const {
    return m_externalRTV ? m_externalRTV : m_rtv.Get();
  }
  ID3D11BlendState *alphaBlendState() const { return m_alphaBlend.Get(); }
  IDXGISwapChain1 *swapChain() const { return m_swapChain.Get(); }

  int width() const { return m_width; }
  int height() const { return m_height; }
  bool isInitialized() const { return m_initialized; }

  // --- Shader helpers ---

  /**
   * @brief Compile an HLSL shader from source string
   * @param source HLSL source code
   * @param entryPoint Shader entry point function name
   * @param target Shader model target (e.g., "vs_5_0", "ps_5_0")
   * @param errorMsg Output: compilation error message if failed
   * @return Compiled shader blob, or nullptr on failure
   */
  ComPtr<ID3DBlob> compileShader(const QByteArray &source,
                                  const char *entryPoint, const char *target,
                                  QString &errorMsg);

  /**
   * @brief Compile an HLSL shader with #include resolution via QRC
   * @param source HLSL source code
   * @param entryPoint Shader entry point function name
   * @param target Shader model target (e.g., "vs_5_0", "ps_5_0")
   * @param qrcBasePath QRC directory for #include resolution
   *        (e.g., ":/shaders/radial")
   * @param errorMsg Output: compilation error message if failed
   * @return Compiled shader blob, or nullptr on failure
   */
  ComPtr<ID3DBlob> compileShaderWithIncludes(const QByteArray &source,
                                              const char *entryPoint,
                                              const char *target,
                                              const QString &qrcBasePath,
                                              QString &errorMsg);

  // --- Texture helpers ---

  /**
   * @brief Create a 2D texture from RGBA pixel data
   * @param width Texture width
   * @param height Texture height
   * @param rgbaData Pointer to RGBA8 pixel data (width * height * 4 bytes)
   * @return Shader resource view, or nullptr on failure
   */
  ComPtr<ID3D11ShaderResourceView> createTextureFromRGBA(int width, int height,
                                                          const void *rgbaData);

  /**
   * @brief Create a 1x1 white texture (default for untextured rendering)
   */
  ComPtr<ID3D11ShaderResourceView> createDefaultWhiteTexture();

  /**
   * @brief Create a sampler state for texture filtering
   */
  ComPtr<ID3D11SamplerState> createLinearSampler();

private:
  bool createDevice();
  bool createSwapChain(HWND hwnd, int width, int height);
  bool createRenderTarget();
  bool createBlendStates();
  bool createRasterizerState();
  void releaseRenderTarget();

  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;
  ComPtr<IDXGISwapChain1> m_swapChain;
  ComPtr<ID3D11RenderTargetView> m_rtv;
  ComPtr<ID3D11BlendState> m_alphaBlend;
  ComPtr<ID3D11RasterizerState> m_rasterizerState;

  HWND m_hwnd = nullptr;
  int m_width = 0;
  int m_height = 0;
  bool m_initialized = false;
  bool m_offscreen = false;  // true = device-only, no swap chain
  ID3D11RenderTargetView *m_externalRTV = nullptr; // Non-owning, set by caller
};
