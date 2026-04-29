#pragma once

/**
 * @brief Per-profile marker activation state and category visibility
 * persistence
 *
 * Manages a single JSON file per profile under markerStateDir():
 *   marker_state/<profileId>.json
 *
 * Format:
 *   { "type": "marker_state", "version": 1,
 *     "categoryVisibility": { "harvest.ore": true, ... },
 *     "activations": { "<GUID>": { "lastActivated": "ISO8601", "uniqueData": 0
 * } }
 *   }
 *
 * Uses debounced saving (500ms) to batch rapid toggles.
 *
 * DO NOT ADD:
 * - Marker management logic (belongs in MarkerManager)
 * - Path resolution (receives path via constructor)
 */

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>

struct ActivationEntry {
  QDateTime lastActivated;
  int uniqueData = 0; // 0=global, charHash=per-char, instanceId=per-instance
};

class ActivationStore : public QObject {
  Q_OBJECT

public:
  /**
   * @param stateDir  Directory for marker_state JSON files (from
   * StorageBackend)
   * @param parent    QObject parent
   */
  explicit ActivationStore(const QString &stateDir, QObject *parent = nullptr);

  // --- Profile lifecycle ---
  void loadForProfile(const QString &profileId);
  void saveNow();

  // --- Category visibility ---
  bool isCategoryVisible(const QString &categoryPath) const;
  void setCategoryVisible(const QString &categoryPath, bool visible);
  QHash<QString, bool> allCategoryVisibility() const;

  // --- Marker activation ---
  bool isActivated(const QUuid &guid, int uniqueData = 0) const;
  void activate(const QUuid &guid, int uniqueData = 0);
  void deactivate(const QUuid &guid, int uniqueData = 0);
  QDateTime activationTime(const QUuid &guid, int uniqueData = 0) const;

  // --- Cleanup ---
  static void deleteProfileState(const QString &stateDir,
                                 const QString &profileId);

signals:
  void categoryVisibilityChanged(const QString &categoryPath, bool visible);
  void markerActivated(const QUuid &guid);
  void stateChanged();

private:
  void scheduleSave();
  QString activationKey(const QUuid &guid, int uniqueData) const;

  QString m_stateDir;
  QString m_currentProfileId;
  QString m_currentFilePath;

  // In-memory state
  QHash<QString, bool> m_categoryVisibility;
  QHash<QString, ActivationEntry> m_activations; // key = "guid+uniqueData"

  // Debounced save timer
  QTimer *m_saveTimer;
};
