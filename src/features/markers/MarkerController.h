#pragma once

#include <QDir>
#include <QObject>
#include <QStandardPaths>

#include "MarkerManager.h"
#include "MarkerModels.h"
#include "MarkerRenderer.h"
#include "MinimapRenderer.h"
#include "core/MumbleLink.h"

class ImageCache;
class Gw2ApiClient;
class MarkerPackRegistry;
class MarkerSettingsManager;

/**
 * @brief Main controller for the marker system
 *
 * DO NOT ADD:
 * - Inline implementations (use MarkerController.cpp)
 */
class MarkerController : public QObject {
  Q_OBJECT

public:
  explicit MarkerController(QObject *parent = nullptr);
  ~MarkerController();

  /**
   * @brief Initialize and load marker packs
   */
  void start();

  /**
   * @brief Stop and cleanup
   */
  void stop();

  /**
   * @brief Set marker packs directory
   */
  void setPacksDirectory(const QString &path);

  /**
   * @brief Set cache directory for extracted .taco archives
   */
  void setCacheDir(const QString &dir);

  /**
   * @brief Reload all marker packs
   */
  void reloadPacks();

  /**
   * @brief Get the marker manager for UI integration
   */
  MarkerManager *manager() { return m_manager; }

  /**
   * @brief Show/hide the marker overlay
   */
  void setVisible(bool visible);
  bool isVisible() const;

  /**
   * @brief Wire the ActivationStore for persistence (call before start())
   */
  void setActivationStore(ActivationStore *store);

  /**
   * @brief Wire the MarkerSettingsManager for persisted pack/category state
   */
  void setMarkerSettings(MarkerSettingsManager *settings);

  /**
   * @brief Load activation state for a specific profile
   * Call when a profile is launched to restore persisted marker state.
   */
  void loadProfileState(const QString &profileId);

  /**
   * @brief Set API key for GW2 API integration
   */
  void setApiKey(const QString &key);

  /**
   * @brief Get the GW2 API client
   */
  Gw2ApiClient *apiClient() { return m_apiClient; }

  /**
   * @brief Get the current packs directory path
   */
  QString packsPath() const { return m_packsPath; }

  /**
   * @brief Get the shared image cache for icon loading
   */
  ImageCache *imageCache() { return m_imageCache; }

  /**
   * @brief Get the minimap renderer for lifecycle wiring
   */
  MinimapRenderer *minimapRenderer() { return m_minimapRenderer; }

  /**
   * @brief Get the online marker pack registry
   */
  MarkerPackRegistry *registry() { return m_registry; }

signals:
  void packsLoaded(int packCount, int markerCount);

private:
  QString defaultPacksPath() const;

  MumbleLink *m_mumble;
  ImageCache *m_imageCache;
  MarkerManager *m_manager;
  MarkerRenderer *m_renderer;
  MinimapRenderer *m_minimapRenderer;
  Gw2ApiClient *m_apiClient;
  MarkerPackRegistry *m_registry;
  QString m_packsPath;
};
