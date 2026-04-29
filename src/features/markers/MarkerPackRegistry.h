#pragma once

/**
 * @brief Online marker pack registry — discovery, download, and version
 * checking
 *
 * Manages a catalog of community marker packs (seeded from TacO's 18-pack
 * list). Supports downloading .taco files, extracting .taco from .zip,
 * and 3-tier version checking (GitHub API → HTTP HEAD → SHA-256).
 *
 * Owned by MarkerController (packs are shared runtime assets, not
 * per-profile data). Downloaded packs go to packsPath/.
 *
 * DO NOT ADD:
 * - Inline implementations (use MarkerPackRegistry.cpp)
 * - UI code (belongs in MarkerPackBrowser)
 */

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

/**
 * @brief Metadata for a downloadable marker pack
 */
struct OnlineMarkerPack {
  QString id;          // Unique pack ID, e.g. "Tekkit"
  QString name;        // Display name
  QString description; // Short description (content tags)
  QString filename;    // Expected filename, e.g. "tw_ALL_IN_ONE.taco"
  QString downloadUrl; // Primary download URL
  QString backupUrl;   // Fallback URL (optional)
  QString author;      // Pack author display name
  QString sourceUrl;   // Author's page / official download site
  bool enabledByDefault = false;
  bool autoDownloadOnInstall = false; // Download on first AIO install

  // Version checking strategy
  enum VersionStrategy { GitHubReleases, HttpHead, None };
  VersionStrategy versionStrategy = None;
  QString githubOwner; // e.g. "LadyElyssa" (GitHubReleases only)
  QString githubRepo;  // e.g. "LadyElyssaTacoTrails" (GitHubReleases only)

  // Runtime state (not persisted in manifest)
  enum Status { NotInstalled, Installed, UpdateAvailable, Downloading, Error };
  Status status = NotInstalled;
  int downloadProgress = 0; // 0-100
  QString installedVersion;
  QString remoteVersion;
  QString errorMessage;
};

/**
 * @brief Registry for online marker pack discovery, download, and updates
 */
class MarkerPackRegistry : public QObject {
  Q_OBJECT

public:
  explicit MarkerPackRegistry(const QString &packsPath,
                              QObject *parent = nullptr);

  // --- Manifest ---

  /** @brief Load manifest: remote → cache → bundled fallback */
  void loadManifest();

  /** @brief Fetch remote manifest (manual refresh). Returns false if cooldown
   * active. */
  bool fetchRemoteManifest();

  /** @brief Whether a manifest check is allowed (12hr cooldown) */
  bool canCheckManifest() const;

  /** @brief Get all known packs */
  const QList<OnlineMarkerPack> &packs() const;

  /** @brief Get the packs directory path */
  QString packsPath() const { return m_packsPath; }

  /** @brief Find a pack by ID (returns nullptr if not found) */
  OnlineMarkerPack *packById(const QString &id);

  // --- Install status ---

  /** @brief Scan packsPath for installed .taco files and update statuses */
  void refreshInstallStatus();

  // --- Version checking ---

  /** @brief Check all packs for updates (background network requests) */
  void checkForUpdates();

  // --- Download ---

  /** @brief Download a specific pack by ID */
  void downloadPack(const QString &packId);

  /** @brief Cancel an in-progress download */
  void cancelDownload(const QString &packId);

  /** @brief Delete an installed pack (removes .taco, cache dir, and version
   * entry) */
  bool deletePack(const QString &packId);

  /** @brief Set cache directory for cleanup during pack deletion */
  void setCacheDir(const QString &dir) { m_cacheDir = dir; }

  /** @brief Auto-download essential packs on first launch */
  void autoDownloadFirstLaunchPacks();

  /** @brief Whether this is the first launch (no version cache) */
  bool isFirstLaunch() const { return m_isFirstLaunch; }

signals:
  void manifestLoaded();
  void packStatusChanged(const QString &packId);
  void downloadProgress(const QString &packId, int percent);
  void downloadFinished(const QString &packId, bool success,
                        const QString &error);
  void updateCheckComplete();
  void manifestFetchFailed(const QString &error);

private:
  // Version checking
  void checkGitHubVersion(OnlineMarkerPack &pack);
  void checkHttpHeadVersion(OnlineMarkerPack &pack);

  // Download handling
  void startDownload(OnlineMarkerPack &pack, const QString &url);
  void handleDownloadComplete(const QString &packId, QNetworkReply *reply);
  bool extractTacoFromZip(const QString &zipPath, const QString &expectedName);

  // Version cache persistence
  void loadVersionCache();
  void saveVersionCache();

  // Remote manifest
  static constexpr const char *kManifestUrl =
      "https://raw.githubusercontent.com/Foboryus/GW2-AIO-Data/main/"
      "marker_packs.json";
  static constexpr int kManifestCooldownHours = 12;

  bool parseManifestJson(const QByteArray &data);
  bool loadCachedManifest();
  void cacheManifest(const QByteArray &data);
  void loadBundledManifest();

  QString m_packsPath;
  QList<OnlineMarkerPack> m_packs;
  QNetworkAccessManager *m_network;
  QMap<QString, QNetworkReply *> m_activeDownloads;
  QJsonObject m_versionCache;
  int m_pendingVersionChecks = 0;
  bool m_isFirstLaunch = false;
  QString m_manifestETag;
  QDateTime m_lastManifestCheck;
  QString m_cacheDir;
};
