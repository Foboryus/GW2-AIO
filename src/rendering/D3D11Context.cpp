/**
 * @file D3D11Context.cpp
 * @brief D3D11 device, swap chain, and render state implementation
 *
 * Initializes D3D11 resources for transparent overlay rendering:
 * - Device with D3D11_CREATE_DEVICE_BGRA_SUPPORT (required for DWM composition)
 * - DXGI 1.2 swap chain with DXGI_ALPHA_MODE_PREMULTIPLIED
 * - Alpha blend state for proper transparency compositing
 *
 * References:
 * - TacO: Custom DX11 engine with WS_EX_LAYERED overlay
 * - Blish HUD: MonoGame (DX11 backend) with borderless transparent window
 * - Both use DWM composition for transparency — we follow the same pattern
 */

#include "D3D11Context.h"

#include <d3dcompiler.h>
#include <dwmapi.h>

#include <QDebug>
#include <QFile>

// Link libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

// ============================================================================
// Constructor / Destructor
// ============================================================================

D3D11Context::D3D11Context() = default;

D3D11Context::~D3D11Context() { shutdown(); }

// ============================================================================
// Initialization
// ============================================================================

bool D3D11Context::initialize(HWND hwnd, const QSize &size) {
  if (m_initialized) {
    qWarning() << "D3D11Context already initialized";
    return true;
  }

  m_hwnd = hwnd;
  m_width = size.width();
  m_height = size.height();

  if (!createDevice()) {
    qCritical() << "D3D11Context: Failed to create D3D11 device";
    return false;
  }

  if (!createSwapChain(hwnd, m_width, m_height)) {
    qCritical() << "D3D11Context: Failed to create swap chain";
    shutdown();
    return false;
  }

  if (!createRenderTarget()) {
    qCritical() << "D3D11Context: Failed to create render target";
    shutdown();
    return false;
  }

  if (!createBlendStates()) {
    qCritical() << "D3D11Context: Failed to create blend states";
    shutdown();
    return false;
  }

  if (!createRasterizerState()) {
    qCritical() << "D3D11Context: Failed to create rasterizer state";
    shutdown();
    return false;
  }

  m_initialized = true;
  qInfo() << "D3D11Context initialized:" << m_width << "x" << m_height;
  return true;
}

void D3D11Context::shutdown() {
  if (m_context) {
    m_context->ClearState();
    m_context->Flush();
  }

  releaseRenderTarget();
  m_swapChain.Reset();
  m_alphaBlend.Reset();
  m_rasterizerState.Reset();
  m_context.Reset();
  m_device.Reset();

  m_initialized = false;
  m_hwnd = nullptr;
  m_width = 0;
  m_height = 0;

  qInfo() << "D3D11Context shutdown";
}

// ============================================================================
// Frame Management
// ============================================================================

void D3D11Context::beginFrame() {
  if (!m_initialized) {
    return;
  }

  // Set render target
  ID3D11RenderTargetView *rtvs[] = {m_rtv.Get()};
  m_context->OMSetRenderTargets(1, rtvs, nullptr);

  // Set alpha blend state
  float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  m_context->OMSetBlendState(m_alphaBlend.Get(), blendFactor, 0xFFFFFFFF);

  // Set viewport
  D3D11_VIEWPORT vp = {};
  vp.Width = static_cast<float>(m_width);
  vp.Height = static_cast<float>(m_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  m_context->RSSetViewports(1, &vp);

  // Set rasterizer state (CULL_NONE for overlay billboards)
  m_context->RSSetState(m_rasterizerState.Get());

  // Clear to fully transparent black (DWM will composite alpha)
  float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
}

void D3D11Context::endFrame() {
  if (!m_initialized) {
    return;
  }

  // Present without VSync blocking.
  // Our rendering runs inside Qt's single-threaded event loop. Present(1)
  // blocks ~16.6ms per call waiting for VSync, starving all other Qt
  // processing (signals, timers, window tracking). DirectComposition + DWM
  // handle compositing and VSync at the desktop level — overlay doesn't need
  // its own VSync. TacO also uses Present(0) (Bedrock/CoRE2/DX11Device.cpp).
  HRESULT hr = m_swapChain->Present(0, 0);

  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    qWarning() << "D3D11 device lost — reinitializing";
    // REVIEW BEFORE BETA: device-lost recovery not yet implemented
    // TODO: Handle device lost (recreate device + resources)
    m_initialized = false;
  }
}

// ============================================================================
// Resize
// ============================================================================

void D3D11Context::resize(const QSize &size) {
  if (!m_initialized || !m_swapChain) {
    return;
  }

  int newWidth = size.width();
  int newHeight = size.height();

  if (newWidth == m_width && newHeight == m_height) {
    return;
  }

  if (newWidth <= 0 || newHeight <= 0) {
    return;
  }

  // Release old render target before resizing
  releaseRenderTarget();
  m_context->Flush();

  HRESULT hr = m_swapChain->ResizeBuffers(0, static_cast<UINT>(newWidth),
                                          static_cast<UINT>(newHeight),
                                          DXGI_FORMAT_UNKNOWN, 0);

  if (FAILED(hr)) {
    qWarning() << "D3D11Context: ResizeBuffers failed:" << Qt::hex << hr;
    return;
  }

  m_width = newWidth;
  m_height = newHeight;

  if (!createRenderTarget()) {
    qWarning() << "D3D11Context: Failed to recreate render target after resize";
  }

  qInfo() << "D3D11Context resized:" << m_width << "x" << m_height;
}

// ============================================================================
// Device Creation
// ============================================================================

bool D3D11Context::createDevice() {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // Required for DWM

#ifdef QT_DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };

  D3D_FEATURE_LEVEL actualLevel;

  HRESULT hr = D3D11CreateDevice(nullptr, // Default adapter
                                 D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 featureLevels, _countof(featureLevels),
                                 D3D11_SDK_VERSION, m_device.GetAddressOf(),
                                 &actualLevel, m_context.GetAddressOf());

  if (FAILED(hr)) {
    qWarning() << "D3D11CreateDevice failed:" << Qt::hex << hr;
    return false;
  }

  qInfo() << "D3D11 device created, feature level:"
          << (actualLevel == D3D_FEATURE_LEVEL_11_1 ? "11.1" : "11.0");
  return true;
}

// ============================================================================
// Swap Chain Creation
// ============================================================================

bool D3D11Context::createSwapChain(HWND hwnd, int width, int height) {
  // Get DXGI factory from device
  ComPtr<IDXGIDevice> dxgiDevice;
  HRESULT hr = m_device.As(&dxgiDevice);
  if (FAILED(hr)) {
    qWarning() << "Failed to get IDXGIDevice";
    return false;
  }

  ComPtr<IDXGIAdapter> adapter;
  hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to get DXGI adapter";
    return false;
  }

  ComPtr<IDXGIFactory2> factory;
  hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr)) {
    qWarning() << "Failed to get IDXGIFactory2";
    return false;
  }

  // Composition swap chain for DirectComposition — the only D3D11 method
  // that supports per-pixel alpha. DXGI_ALPHA_MODE_PREMULTIPLIED tells DComp
  // to use the alpha channel for compositing. The swap chain is bound to the
  // HWND via IDCompositionVisual::SetContent (done in D3D11OverlayWindow).
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  desc.Flags = 0;

  hr = factory->CreateSwapChainForComposition(m_device.Get(), &desc, nullptr,
                                              m_swapChain.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "CreateSwapChainForComposition failed:" << Qt::hex << hr;
    return false;
  }

  qInfo() << "D3D11: Composition swap chain created (premultiplied alpha)";
  return true;
}

// ============================================================================
// Render Target
// ============================================================================

bool D3D11Context::createRenderTarget() {
  ComPtr<ID3D11Texture2D> backBuffer;
  HRESULT hr =
      m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));

  if (FAILED(hr)) {
    qWarning() << "Failed to get swap chain back buffer:" << Qt::hex << hr;
    return false;
  }

  hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                        m_rtv.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create render target view:" << Qt::hex << hr;
    return false;
  }

  return true;
}

void D3D11Context::releaseRenderTarget() {
  if (m_context) {
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
  }
  m_rtv.Reset();
}

// ============================================================================
// Blend States
// ============================================================================

bool D3D11Context::createBlendStates() {
  // Alpha blend state for transparent overlay rendering
  // Source pixels are premultiplied alpha (matches DWM composition)
  D3D11_BLEND_DESC blendDesc = {};
  blendDesc.AlphaToCoverageEnable = FALSE;
  blendDesc.IndependentBlendEnable = FALSE;

  auto &rt = blendDesc.RenderTarget[0];
  rt.BlendEnable = TRUE;
  rt.SrcBlend = D3D11_BLEND_ONE; // Premultiplied alpha
  rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOp = D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D11_BLEND_ONE;
  rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  HRESULT hr =
      m_device->CreateBlendState(&blendDesc, m_alphaBlend.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create alpha blend state:" << Qt::hex << hr;
    return false;
  }

  return true;
}

// ============================================================================
// Shader Compilation
// ============================================================================

ComPtr<ID3DBlob> D3D11Context::compileShader(const QByteArray &source,
                                             const char *entryPoint,
                                             const char *target,
                                             QString &errorMsg) {
  ComPtr<ID3DBlob> shaderBlob;
  ComPtr<ID3DBlob> errorBlob;

  UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef QT_DEBUG
  compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  HRESULT hr = D3DCompile(source.constData(), source.size(), nullptr, nullptr,
                          nullptr, entryPoint, target, compileFlags, 0,
                          shaderBlob.GetAddressOf(), errorBlob.GetAddressOf());

  if (FAILED(hr)) {
    if (errorBlob) {
      errorMsg = QString::fromUtf8(
          static_cast<const char *>(errorBlob->GetBufferPointer()),
          static_cast<int>(errorBlob->GetBufferSize()));
    } else {
      errorMsg =
          QString("Shader compilation failed: 0x%1").arg(hr, 8, 16, QChar('0'));
    }
    return nullptr;
  }

  return shaderBlob;
}

// ============================================================================
// QRC-based ID3DInclude for shader #include resolution
// ============================================================================

namespace {

/**
 * @brief Custom ID3DInclude that resolves #include directives from QRC
 *
 * When a shader does #include "radial_noise.hlsl", this handler reads
 * the file from Qt resources (e.g., ":/shaders/radial/radial_noise.hlsl").
 */
class QrcInclude : public ID3DInclude {
public:
  explicit QrcInclude(const QString &basePath) : m_basePath(basePath) {}

  HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE /*includeType*/,
                                  LPCSTR pFileName,
                                  LPCVOID /*pParentData*/,
                                  LPCVOID *ppData,
                                  UINT *pBytes) override {
    QString path = m_basePath + "/" + QString::fromUtf8(pFileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      qWarning() << "QrcInclude: failed to open" << path;
      return E_FAIL;
    }

    QByteArray data = file.readAll();
    file.close();

    // Allocate buffer that D3DCompile will use (freed in Close)
    auto *buf = new char[data.size()];
    memcpy(buf, data.constData(), data.size());

    *ppData = buf;
    *pBytes = static_cast<UINT>(data.size());
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Close(LPCVOID pData) override {
    delete[] static_cast<const char *>(pData);
    return S_OK;
  }

private:
  QString m_basePath;
};

} // namespace

ComPtr<ID3DBlob> D3D11Context::compileShaderWithIncludes(
    const QByteArray &source, const char *entryPoint, const char *target,
    const QString &qrcBasePath, QString &errorMsg) {
  ComPtr<ID3DBlob> shaderBlob;
  ComPtr<ID3DBlob> errorBlob;

  UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef QT_DEBUG
  compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  QrcInclude includeHandler(qrcBasePath);

  HRESULT hr = D3DCompile(source.constData(), source.size(), nullptr, nullptr,
                           &includeHandler, entryPoint, target, compileFlags, 0,
                           shaderBlob.GetAddressOf(), errorBlob.GetAddressOf());

  if (FAILED(hr)) {
    if (errorBlob) {
      errorMsg = QString::fromUtf8(
          static_cast<const char *>(errorBlob->GetBufferPointer()),
          static_cast<int>(errorBlob->GetBufferSize()));
    } else {
      errorMsg =
          QString("Shader compilation failed: 0x%1").arg(hr, 8, 16, QChar('0'));
    }
    return nullptr;
  }

  return shaderBlob;
}

// ============================================================================
// Texture Helpers
// ============================================================================

ComPtr<ID3D11ShaderResourceView>
D3D11Context::createTextureFromRGBA(int width, int height,
                                    const void *rgbaData) {
  if (!m_device || !rgbaData || width <= 0 || height <= 0) {
    return nullptr;
  }

  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = static_cast<UINT>(width);
  texDesc.Height = static_cast<UINT>(height);
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_IMMUTABLE;
  texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA initData = {};
  initData.pSysMem = rgbaData;
  initData.SysMemPitch = static_cast<UINT>(width * 4);

  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr =
      m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create texture:" << Qt::hex << hr;
    return nullptr;
  }

  ComPtr<ID3D11ShaderResourceView> srv;
  hr = m_device->CreateShaderResourceView(texture.Get(), nullptr,
                                          srv.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create SRV:" << Qt::hex << hr;
    return nullptr;
  }

  return srv;
}

ComPtr<ID3D11ShaderResourceView> D3D11Context::createDefaultWhiteTexture() {
  uint32_t white = 0xFFFFFFFF; // RGBA: 255, 255, 255, 255
  return createTextureFromRGBA(1, 1, &white);
}

ComPtr<ID3D11SamplerState> D3D11Context::createLinearSampler() {
  D3D11_SAMPLER_DESC samplerDesc = {};
  samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

  ComPtr<ID3D11SamplerState> sampler;
  HRESULT hr =
      m_device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create sampler state:" << Qt::hex << hr;
    return nullptr;
  }

  return sampler;
}

bool D3D11Context::createRasterizerState() {
  D3D11_RASTERIZER_DESC rsDesc = {};
  rsDesc.FillMode = D3D11_FILL_SOLID;
  rsDesc.CullMode = D3D11_CULL_NONE; // Overlays: never cull billboards
  rsDesc.FrontCounterClockwise = FALSE;
  rsDesc.DepthBias = 0;
  rsDesc.DepthBiasClamp = 0.0f;
  rsDesc.SlopeScaledDepthBias = 0.0f;
  rsDesc.DepthClipEnable = TRUE;
  rsDesc.ScissorEnable = FALSE;
  rsDesc.MultisampleEnable = FALSE;
  rsDesc.AntialiasedLineEnable = FALSE;

  HRESULT hr = m_device->CreateRasterizerState(
      &rsDesc, m_rasterizerState.GetAddressOf());
  if (FAILED(hr)) {
    qWarning() << "Failed to create rasterizer state:" << Qt::hex << hr;
    return false;
  }

  return true;
}
