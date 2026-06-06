#pragma once

/**
 * @file RadialRenderer.h
 * @brief GPU resource manager for radial wheel D3D11 rendering
 *
 * Owns compiled shaders, constant buffers, sampler states, and textures.
 * Provides draw methods that set pipeline state and issue draw calls.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialRenderer.cpp)
 * - Wheel logic (belongs in RadialWheel)
 * - Window management (belongs in RadialOverlayWindow)
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <d3d11.h>
#include <wrl/client.h>

#include <QString>

using Microsoft::WRL::ComPtr;

class D3D11Context;
class RadialWheel;
struct RadialElement;

class RadialRenderer {
public:
  RadialRenderer();
  ~RadialRenderer();

  // Non-copyable
  RadialRenderer(const RadialRenderer &) = delete;
  RadialRenderer &operator=(const RadialRenderer &) = delete;

  /**
   * @brief Initialize GPU resources (shaders, CBs, textures)
   * @return true if all resources created successfully
   */
  bool initialize(D3D11Context *ctx);

  /**
   * @brief Release all GPU resources
   */
  void shutdown();

  /**
   * @brief Check if initialized
   */
  bool isInitialized() const { return m_initialized; }

  // --- Texture Loading ---

  /**
   * @brief Load an icon from a QRC PNG path into a D3D11 texture
   * @param ctx D3D11 context for texture creation
   * @param qrcPath QRC path (e.g., ":/radial/png/Raptor.png")
   * @return SRV for the loaded texture, or nullptr on failure
   */
  ComPtr<ID3D11ShaderResourceView> loadIconTexture(D3D11Context *ctx,
                                                    const QString &qrcPath);

  /**
   * @brief Load an icon with a debug text label burned into the texture.
   * @param ctx D3D11 context for texture creation
   * @param qrcPath QRC path to the icon
   * @param label Text to render on the icon (e.g., "Raptor\nCtrl+R")
   * @return SRV for the loaded texture, or nullptr on failure
   *
   * TEMPORARY: For debugging keybind-to-icon mapping issues.
   */
  ComPtr<ID3D11ShaderResourceView> loadIconTextureWithLabel(
      D3D11Context *ctx, const QString &qrcPath, const QString &label);

  /**
   * @brief Create a text-only label texture for elements without icons.
   * @param ctx D3D11 context for texture creation
   * @param label Text to render (e.g., "S" for skiff)
   * @return SRV for the created texture, or nullptr on failure
   */
  ComPtr<ID3D11ShaderResourceView> createLabelTexture(
      D3D11Context *ctx, const QString &label);

  // --- Draw Methods ---

  /**
   * @brief Draw the background wheel ring
   */
  void drawWheel(D3D11Context *ctx, RadialWheel *wheel);

  /**
   * @brief Draw colored transparent sectors for debug visualization.
   * Each element gets a unique color wedge extending to the screen edges.
   *
   * TEMPORARY: For debugging which angular section maps to which mount.
   *
   * @param elementCount Number of visible elements
   * @param hoveredIndex Currently hovered index (-1 for none)
   */
  void drawDebugSectors(D3D11Context *ctx, int elementCount,
                        int hoveredIndex);

  /**
   * @brief Draw a single element icon slice
   */
  void drawElement(D3D11Context *ctx, RadialWheel *wheel,
                   RadialElement *element, int index, int totalElements);

  /**
   * @brief Draw the cursor glow at mouse position
   */
  void drawCursor(D3D11Context *ctx, int mouseX, int mouseY, int screenW,
                  int screenH, float animationTimer, float globalOpacity);

  /**
   * @brief Set the wheel scale (fraction of screen height)
   * Default: 0.72 (72% of screen height)
   */
  void setWheelScale(float scale) { m_wheelScale = scale; }

private:
  /**
   * @brief Compile all shaders from QRC
   */
  bool compileShaders(D3D11Context *ctx);

  /**
   * @brief Create constant buffers
   */
  bool createConstantBuffers(D3D11Context *ctx);

  /**
   * @brief Generate and load placeholder background texture
   */
  bool createBackgroundTexture(D3D11Context *ctx);

  /**
   * @brief Update the VS constant buffer with sprite dimensions
   */
  void updateQuadCB(D3D11Context *ctx, float centerX, float centerY,
                    float halfW, float halfH, bool tilt, int mouseX,
                    int mouseY, int screenW, int screenH);

  /**
   * @brief Update the PS wheel constant buffer
   */
  void updateWheelCB(D3D11Context *ctx, RadialWheel *wheel);

  /**
   * @brief Update the PS element constant buffer
   */
  void updateElementCB(D3D11Context *ctx, RadialElement *element);

  bool m_initialized = false;
  float m_wheelScale = 0.72f;  // Configurable via setWheelScale()

  // Shaders
  ComPtr<ID3D11VertexShader> m_radialVS;
  ComPtr<ID3D11PixelShader> m_wheelPS;
  ComPtr<ID3D11PixelShader> m_elementPS;
  ComPtr<ID3D11PixelShader> m_cursorPS;
  ComPtr<ID3D11PixelShader> m_delayPS;

  // Constant buffers
  ComPtr<ID3D11Buffer> m_quadCB;   // VS b0: sprite dimensions + tilt
  ComPtr<ID3D11Buffer> m_wheelCB;  // PS b0: wheel state
  ComPtr<ID3D11Buffer> m_elemCB;   // PS b1: per-element state

  // Textures
  ComPtr<ID3D11ShaderResourceView> m_backgroundSRV;
  ComPtr<ID3D11SamplerState> m_sampler;

  // Blend state (premultiplied alpha for DComp)
  ComPtr<ID3D11BlendState> m_blendState;

  // TEMPORARY: Debug sector overlay texture (cached)
  ComPtr<ID3D11ShaderResourceView> m_debugSectorSRV;
  int m_debugSectorCount = 0;  // Invalidation key
};
