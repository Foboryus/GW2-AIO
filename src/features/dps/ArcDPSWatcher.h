#pragma once

#include <QDir>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStandardPaths>
#include <QTimer>


#include "ArcDPSModels.h"
#include "CombatTracker.h"
#include "EVTCParser.h"


namespace ArcDPS {

/**
 * @brief Monitors ArcDPS log directory for new logs
 *
 * ArcDPS saves logs to:
 * - Documents/Guild Wars 2/addons/arcdps/arcdps.cbtlogs/
 *
 * DO NOT ADD:
 * - Inline implementations (use ArcDPSWatcher.cpp)
 */
class LogWatcher : public QObject {
  Q_OBJECT

public:
  explicit LogWatcher(QObject *parent = nullptr);

  /**
   * @brief Set the log directory to watch
   */
  void setLogDirectory(const QString &path);

  /**
   * @brief Get the default ArcDPS log directory
   */
  static QString defaultLogDirectory();

  /**
   * @brief Start watching for new logs
   */
  void start();

  /**
   * @brief Stop watching
   */
  void stop();

  /**
   * @brief Get recent logs
   */
  const QList<ParsedLog> &recentLogs() const { return m_recentLogs; }

  /**
   * @brief Parse a specific log file
   */
  ParsedLog parseLog(const QString &filePath);

signals:
  void newLogDetected(const QString &filePath);
  void logParsed(const ParsedLog &log);

private slots:
  void onDirectoryChanged(const QString &path);
  void checkForNewLogs();

private:
  QFileSystemWatcher *m_watcher;
  EVTCParser *m_parser;
  QTimer *m_pollTimer;

  QString m_logDirectory;
  QStringList m_knownFiles;
  QList<ParsedLog> m_recentLogs;
  int m_maxRecentLogs = 20;
};

} // namespace ArcDPS
