#pragma once

#include <QDir>
#include <QFile>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStandardPaths>
#include <QString>


/**
 * @brief Keybind conflict detector
 *
 * Warns if AIO hotkeys conflict with GW2 keybinds.
 *
 * DO NOT ADD:
 * - Inline implementations (use KeybindConflictDetector.cpp)
 */
class KeybindConflictDetector : public QObject {
  Q_OBJECT

public:
  explicit KeybindConflictDetector(QObject *parent = nullptr);

  /**
   * @brief Load GW2 keybinds from config
   */
  bool loadGW2Keybinds(const QString &gw2Path);

  /**
   * @brief Check if a keybind conflicts
   */
  bool hasConflict(const QString &keybind) const;

  /**
   * @brief Get conflict info
   */
  struct ConflictInfo {
    QString keybind;
    QString gw2Action;
    QString aioAction;
  };
  QList<ConflictInfo>
  checkConflicts(const QMap<QString, QString> &aioBinds) const;

  /**
   * @brief Get GW2 action for keybind
   */
  QString getGW2Action(const QString &keybind) const;

signals:
  void conflictsDetected(const QList<ConflictInfo> &conflicts);

private:
  void parseInputBindings(const QString &content);
  QString normalizeKeybind(const QString &keybind) const;

  // GW2 keybind -> action name
  QMap<QString, QString> m_gw2Keybinds;

  // Known GW2 action names
  QMap<QString, QString> m_actionNames;
};
