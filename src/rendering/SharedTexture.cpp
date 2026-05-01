/**
 * @file SharedTexture.cpp
 * @brief Cross-process D3D11 shared texture implementation
 *
 * Implements SharedTextureProducer and SharedTextureConsumer for the
 * compositor architecture. Uses D3D11.1 NT named handles for cross-process
 * texture sharing and IDXGIKeyedMutex for GPU-side synchronization.
 *
 * Key protocol:
 * - Producer: AcquireSync(0) → write → Flush → ReleaseSync(1)
 * - Consumer: AcquireSync(1) → read  →         ReleaseSync(0)
 * - Key 0 = producer's turn, Key 1 = consumer's turn
 *
 * References:
 * - Chrome compositor: same NT handle + KeyedMutex pattern
 * - OBS game capture: same cross-process shared texture approach
 */

#include "SharedTexture.h"

#include <QDebug>

// ============================================================================
// SharedTextureProducer
// ============================================================================

SharedTextureProducer::SharedTextureProducer() = default;

SharedTextureProducer::~SharedTextureProducer() { shutdown(); }

bool SharedTextureProducer::initialize(ID3D11Device *device,
                                       ID3D11DeviceContext *context,
                                       int width, int height,
                                       const QString &name) {
  if (m_initialized) {
    qWarning() << "SharedTextureProducer: already initialized";
    return true;
  }

  if (!device || !context) {
    qCritical() << "SharedTextureProducer: null device or context";
    return false;
  }

  if (width <= 0 || height <= 0) {
    qCritical() << "SharedTextureProducer: invalid dimensions"
                << width << "x" << height;
    return false;
  }

  m_device = device;
  m_context = context;
  m_name = name;

  if (!createSharedTexture(width, height)) {
    return false;
  }

  m_initialized = true;
  qInfo() << "SharedTextureProducer: initialized"
          << m_width << "x" << m_height
          << "name:" << m_name;
  return true;
}

void SharedTextureProducer::shutdown() {
  if (m_acquired) {
    releaseAfterWrite();
  }

  releaseSharedTexture();

  m_device = nullptr;
  m_context = nullptr;
  m_initialized = false;

  qInfo() << "SharedTextureProducer: shutdown" << m_name;
}

bool SharedTextureProducer::createSharedTexture(int width, int height) {
  // Texture desc: shared + keyed mutex for cross-process sync
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Matches DWM/Qt premultiplied
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                   D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  HRESULT hr = m_device->CreateTexture2D(&desc, nullptr,
                                         m_texture.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SharedTextureProducer: CreateTexture2D failed:"
                << Qt::hex << hr;
    return false;
  }

  // Get KeyedMutex interface for synchronization
  hr = m_texture.As(&m_keyedMutex);
  if (FAILED(hr)) {
    qCritical() << "SharedTextureProducer: failed to get IDXGIKeyedMutex:"
                << Qt::hex << hr;
    m_texture.Reset();
    return false;
  }

  // Create named NT handle for cross-process sharing
  ComPtr<IDXGIResource1> dxgiResource;
  hr = m_texture.As(&dxgiResource);
  if (FAILED(hr)) {
    qCritical() << "SharedTextureProducer: failed to get IDXGIResource1:"
                << Qt::hex << hr;
    m_keyedMutex.Reset();
    m_texture.Reset();
    return false;
  }

  std::wstring nameW = m_name.toStdWString();
  hr = dxgiResource->CreateSharedHandle(
      nullptr,  // Default security
      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
      nameW.c_str(),
      &m_sharedHandle);
  if (FAILED(hr)) {
    qCritical() << "SharedTextureProducer: CreateSharedHandle failed:"
                << Qt::hex << hr << "name:" << m_name;
    m_keyedMutex.Reset();
    m_texture.Reset();
    return false;
  }

  // Create render target view for writing
  hr = m_device->CreateRenderTargetView(m_texture.Get(), nullptr,
                                        m_rtv.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SharedTextureProducer: CreateRenderTargetView failed:"
                << Qt::hex << hr;
    CloseHandle(m_sharedHandle);
    m_sharedHandle = nullptr;
    m_keyedMutex.Reset();
    m_texture.Reset();
    return false;
  }

  m_width = width;
  m_height = height;

  qInfo() << "SharedTextureProducer: shared texture created"
          << width << "x" << height
          << "handle:" << m_sharedHandle;
  return true;
}

void SharedTextureProducer::releaseSharedTexture() {
  m_rtv.Reset();
  m_keyedMutex.Reset();
  m_texture.Reset();

  if (m_sharedHandle) {
    CloseHandle(m_sharedHandle);
    m_sharedHandle = nullptr;
  }

  m_width = 0;
  m_height = 0;
}

ID3D11RenderTargetView *SharedTextureProducer::acquireForWrite(DWORD timeoutMs) {
  if (!m_initialized || !m_keyedMutex) {
    return nullptr;
  }

  if (m_acquired) {
    qWarning() << "SharedTextureProducer: already acquired for write";
    return m_rtv.Get();
  }

  // Key 0 = producer's turn to write
  HRESULT hr = m_keyedMutex->AcquireSync(0, timeoutMs);
  if (hr == WAIT_TIMEOUT) {
    // Consumer hasn't released yet — skip this frame
    return nullptr;
  }
  if (FAILED(hr)) {
    qWarning() << "SharedTextureProducer: AcquireSync failed:"
               << Qt::hex << hr;
    return nullptr;
  }

  m_acquired = true;
  return m_rtv.Get();
}

void SharedTextureProducer::releaseAfterWrite() {
  if (!m_acquired || !m_keyedMutex) {
    return;
  }

  // CRITICAL: Flush before releasing mutex.
  // Without this, the consumer may read stale GPU data because
  // the producer's draw calls haven't been submitted to the GPU yet.
  if (m_context) {
    m_context->Flush();
  }

  // Key 1 = consumer's turn to read
  m_keyedMutex->ReleaseSync(1);
  m_acquired = false;
}

bool SharedTextureProducer::resize(int width, int height) {
  if (!m_initialized) {
    return false;
  }

  if (width == m_width && height == m_height) {
    return true;  // No-op
  }

  if (width <= 0 || height <= 0) {
    qWarning() << "SharedTextureProducer: invalid resize dimensions"
               << width << "x" << height;
    return false;
  }

  qInfo() << "SharedTextureProducer: resizing"
          << m_width << "x" << m_height
          << "→" << width << "x" << height;

  // Release old resources (must not be acquired)
  if (m_acquired) {
    releaseAfterWrite();
  }
  releaseSharedTexture();

  // Recreate with same name — consumer must reopen()
  if (!createSharedTexture(width, height)) {
    qCritical() << "SharedTextureProducer: resize failed";
    m_initialized = false;
    return false;
  }

  return true;
}

// ============================================================================
// SharedTextureConsumer
// ============================================================================

SharedTextureConsumer::SharedTextureConsumer() = default;

SharedTextureConsumer::~SharedTextureConsumer() { shutdown(); }

bool SharedTextureConsumer::open(ID3D11Device1 *device, const QString &name) {
  if (m_opened) {
    qWarning() << "SharedTextureConsumer: already open, call shutdown() first";
    return false;
  }

  if (!device) {
    qCritical() << "SharedTextureConsumer: null device";
    return false;
  }

  m_device = device;
  m_name = name;

  // Open the shared texture by name
  std::wstring nameW = m_name.toStdWString();
  HRESULT hr = m_device->OpenSharedResourceByName(
      nameW.c_str(),
      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
      IID_PPV_ARGS(m_texture.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "SharedTextureConsumer: OpenSharedResourceByName failed:"
               << Qt::hex << hr << "name:" << m_name;
    return false;
  }

  // Get KeyedMutex for synchronization
  hr = m_texture.As(&m_keyedMutex);
  if (FAILED(hr)) {
    qCritical() << "SharedTextureConsumer: failed to get IDXGIKeyedMutex:"
                << Qt::hex << hr;
    m_texture.Reset();
    return false;
  }

  // Create staging texture (local copy for zero-contention rendering)
  if (!createStagingTexture()) {
    qCritical() << "SharedTextureConsumer: failed to create staging texture";
    m_keyedMutex.Reset();
    m_texture.Reset();
    return false;
  }

  m_opened = true;
  qInfo() << "SharedTextureConsumer: opened shared texture"
          << "name:" << m_name;
  return true;
}

bool SharedTextureConsumer::createStagingTexture() {
  // Query the shared texture's dimensions
  D3D11_TEXTURE2D_DESC sharedDesc = {};
  m_texture->GetDesc(&sharedDesc);

  // Create a local texture with the same size/format (not shared, no mutex)
  D3D11_TEXTURE2D_DESC stagingDesc = {};
  stagingDesc.Width = sharedDesc.Width;
  stagingDesc.Height = sharedDesc.Height;
  stagingDesc.MipLevels = 1;
  stagingDesc.ArraySize = 1;
  stagingDesc.Format = sharedDesc.Format;  // DXGI_FORMAT_B8G8R8A8_UNORM
  stagingDesc.SampleDesc.Count = 1;
  stagingDesc.SampleDesc.Quality = 0;
  stagingDesc.Usage = D3D11_USAGE_DEFAULT;
  stagingDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  stagingDesc.MiscFlags = 0;  // Not shared — local to compositor

  ComPtr<ID3D11Device> baseDevice;
  m_device->QueryInterface(IID_PPV_ARGS(baseDevice.GetAddressOf()));

  HRESULT hr = baseDevice->CreateTexture2D(&stagingDesc, nullptr,
                                           m_stagingTexture.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SharedTextureConsumer: CreateTexture2D (staging) failed:"
                << Qt::hex << hr;
    return false;
  }

  // Create SRV for the staging texture (used in compositor's Draw call)
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = sharedDesc.Format;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  hr = baseDevice->CreateShaderResourceView(m_stagingTexture.Get(), &srvDesc,
                                            m_stagingSrv.GetAddressOf());
  if (FAILED(hr)) {
    qCritical() << "SharedTextureConsumer: CreateSRV (staging) failed:"
                << Qt::hex << hr;
    m_stagingTexture.Reset();
    return false;
  }

  qInfo() << "SharedTextureConsumer: staging texture created"
          << sharedDesc.Width << "x" << sharedDesc.Height;
  return true;
}

void SharedTextureConsumer::shutdown() {
  if (m_acquired) {
    // Safety: release if somehow still acquired (shouldn't happen with staging)
    m_keyedMutex->ReleaseSync(0);
    m_acquired = false;
  }

  m_stagingSrv.Reset();
  m_stagingTexture.Reset();
  m_srv.Reset();
  m_keyedMutex.Reset();
  m_texture.Reset();

  m_device = nullptr;
  m_opened = false;
  m_hasValidStaging = false;

  qInfo() << "SharedTextureConsumer: shutdown" << m_name;
}

ID3D11ShaderResourceView *SharedTextureConsumer::acquireForRead(DWORD timeoutMs) {
  if (!m_opened || !m_keyedMutex || !m_stagingTexture) {
    return nullptr;
  }

  // Key 1 = consumer's turn to read
  HRESULT hr = m_keyedMutex->AcquireSync(1, timeoutMs);
  if (hr == WAIT_TIMEOUT) {
    // Producer busy writing — return LAST GOOD staging copy if available.
    // The staging texture retains the previous frame's content, so showing
    // a 1-frame-stale image is vastly better than a transparent hole (flicker).
    return m_hasValidStaging ? m_stagingSrv.Get() : nullptr;
  }
  if (FAILED(hr)) {
    // Hard failure (device lost, etc.) — return last good frame if available
    return m_hasValidStaging ? m_stagingSrv.Get() : nullptr;
  }

  // Copy shared texture → local staging texture
  // This is a GPU-side copy (~0.1ms), much faster than the producer's render
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<ID3D11Device> baseDevice;
  m_device->QueryInterface(IID_PPV_ARGS(baseDevice.GetAddressOf()));
  baseDevice->GetImmediateContext(ctx.GetAddressOf());

  ctx->CopyResource(m_stagingTexture.Get(), m_texture.Get());
  m_hasValidStaging = true;

  // Release mutex IMMEDIATELY — producer can start writing the next frame
  // Key 0 = producer's turn to write
  m_keyedMutex->ReleaseSync(0);

  // Return SRV of the staging copy — compositor can hold this as long as needed
  return m_stagingSrv.Get();
}

void SharedTextureConsumer::releaseAfterRead() {
  // No-op: mutex was released inside acquireForRead() after the staging copy.
  // Kept for API compatibility with the compositor render loop.
}

bool SharedTextureConsumer::reopen() {
  if (!m_device || m_name.isEmpty()) {
    qWarning() << "SharedTextureConsumer: cannot reopen — no device or name";
    return false;
  }

  QString savedName = m_name;
  ID3D11Device1 *savedDevice = m_device;

  shutdown();
  return open(savedDevice, savedName);
}

