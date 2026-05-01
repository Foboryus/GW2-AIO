#pragma once

/**
 * @file SharedTexture.h
 * @brief Cross-process D3D11 shared texture for compositor architecture
 *
 * Provides SharedTextureProducer (child processes) and SharedTextureConsumer
 * (compositor process) for GPU-side texture sharing via NT named handles
 * and IDXGIKeyedMutex synchronization.
 *
 * Usage:
 * - Producer: Create texture → acquireForWrite → render → releaseAfterWrite
 * - Consumer: Open by name → acquireForRead → sample SRV → releaseAfterRead
 *
 * Naming: L"GW2AIO_Tex_<profileUUID>_<layerType>"
 *   layerType: "3d", "minimap", "radial", "hud"
 *
 * Requirements:
 * - D3D11.1 (feature level 11.0+)
 * - Both processes must use the same GPU adapter
 * - Producer must call Flush() before releasing mutex (handled internally)
 *
 * DO NOT ADD:
 * - Inline implementations (use SharedTexture.cpp)
 * - Rendering logic (belongs in pipeline/child classes)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <QString>

using Microsoft::WRL::ComPtr;

/**
 * @brief Producer side — creates a named shared D3D11 texture for writing.
 *
 * Used by feature child processes (3D, minimap, radial, HUD) to render
 * offscreen. The compositor opens this texture by name and composites it.
 */
class SharedTextureProducer {
public:
  SharedTextureProducer();
  ~SharedTextureProducer();

  // Non-copyable
  SharedTextureProducer(const SharedTextureProducer &) = delete;
  SharedTextureProducer &operator=(const SharedTextureProducer &) = delete;

  /**
   * @brief Initialize the shared texture
   * @param device D3D11 device (must support feature level 11.0+)
   * @param context Device context (for Flush on release)
   * @param width Texture width in pixels
   * @param height Texture height in pixels
   * @param name Global name for cross-process discovery
   *        (e.g., L"GW2AIO_Tex_<uuid>_3d")
   * @return true if texture and named handle were created successfully
   */
  bool initialize(ID3D11Device *device, ID3D11DeviceContext *context,
                  int width, int height, const QString &name);

  /**
   * @brief Release all resources
   */
  void shutdown();

  /**
   * @brief Acquire the texture for writing (blocks until available)
   * @param timeoutMs Maximum wait time in milliseconds (0 = no wait)
   * @return Render target view, or nullptr if acquire failed/timed out
   */
  ID3D11RenderTargetView *acquireForWrite(DWORD timeoutMs = 16);

  /**
   * @brief Release the texture after writing
   *
   * Calls Flush() on the device context before releasing the mutex
   * to ensure GPU commands are visible to the consumer process.
   */
  void releaseAfterWrite();

  /**
   * @brief Resize the shared texture (destroys and recreates)
   * @param width New width
   * @param height New height
   * @return true if resize succeeded
   *
   * @note The consumer must call reopen() after this to pick up
   * the new texture. Coordinate via IPC (RESIZE message).
   */
  bool resize(int width, int height);

  // --- Accessors ---
  ID3D11Texture2D *texture() const { return m_texture.Get(); }
  ID3D11RenderTargetView *rtv() const { return m_rtv.Get(); }
  int width() const { return m_width; }
  int height() const { return m_height; }
  bool isInitialized() const { return m_initialized; }
  const QString &name() const { return m_name; }

private:
  bool createSharedTexture(int width, int height);
  void releaseSharedTexture();

  ID3D11Device *m_device = nullptr;           // Non-owning
  ID3D11DeviceContext *m_context = nullptr;    // Non-owning
  ComPtr<ID3D11Texture2D> m_texture;
  ComPtr<ID3D11RenderTargetView> m_rtv;
  ComPtr<IDXGIKeyedMutex> m_keyedMutex;
  HANDLE m_sharedHandle = nullptr;

  QString m_name;
  int m_width = 0;
  int m_height = 0;
  bool m_initialized = false;
  bool m_acquired = false;
};

/**
 * @brief Consumer side — opens a named shared D3D11 texture for reading.
 *
 * Uses a staging copy pattern to minimize mutex hold time:
 * acquireForRead() acquires the mutex, copies the shared texture to a local
 * staging texture, then releases the mutex immediately. The returned SRV
 * points to the staging copy — the shared texture mutex is NOT held during
 * the compositor's Draw call.
 *
 * This reduces mutex contention from ~3ms (full render) to ~0.1ms (copy),
 * eliminating the ~44% frame drop rate that caused visible flicker.
 */
class SharedTextureConsumer {
public:
  SharedTextureConsumer();
  ~SharedTextureConsumer();

  // Non-copyable
  SharedTextureConsumer(const SharedTextureConsumer &) = delete;
  SharedTextureConsumer &operator=(const SharedTextureConsumer &) = delete;

  /**
   * @brief Open a shared texture by name
   * @param device D3D11.1 device (ID3D11Device1 required for OpenSharedResourceByName)
   * @param name Name used by the producer's CreateSharedHandle
   * @return true if the texture was opened and SRV created
   */
  bool open(ID3D11Device1 *device, const QString &name);

  /**
   * @brief Release all resources
   */
  void shutdown();

  /**
   * @brief Copy the latest frame from the shared texture (non-blocking)
   *
   * Acquires the keyed mutex, copies to a local staging texture, releases
   * the mutex immediately. Returns SRV of the staging copy.
   *
   * @param timeoutMs Maximum wait time (0 = try once, don't block)
   * @return Shader resource view of the staging copy, or nullptr if failed
   */
  ID3D11ShaderResourceView *acquireForRead(DWORD timeoutMs = 0);

  /**
   * @brief No-op — mutex is released immediately after staging copy.
   *
   * Kept for API compatibility. The staging copy pattern releases the mutex
   * inside acquireForRead(), so no explicit release is needed.
   */
  void releaseAfterRead();

  /**
   * @brief Re-open the texture (call after producer resizes)
   * @return true if re-open succeeded
   */
  bool reopen();

  // --- Accessors ---
  ID3D11Texture2D *texture() const { return m_texture.Get(); }
  ID3D11ShaderResourceView *srv() const { return m_stagingSrv.Get(); }
  bool isOpen() const { return m_opened; }
  const QString &name() const { return m_name; }

private:
  bool createStagingTexture();

  ID3D11Device1 *m_device = nullptr;          // Non-owning
  ComPtr<ID3D11Texture2D> m_texture;          // Shared texture (cross-process)
  ComPtr<IDXGIKeyedMutex> m_keyedMutex;

  // Staging copy — local texture on compositor's device
  ComPtr<ID3D11Texture2D> m_stagingTexture;
  ComPtr<ID3D11ShaderResourceView> m_stagingSrv;

  // Legacy SRV on the shared texture — kept for fallback but unused
  ComPtr<ID3D11ShaderResourceView> m_srv;

  QString m_name;
  bool m_opened = false;
  bool m_acquired = false;  // Legacy — always false with staging copy
  bool m_hasValidStaging = false;  // True after first successful copy
};
