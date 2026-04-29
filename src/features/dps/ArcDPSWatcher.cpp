/**
 * @file ArcDPSWatcher.cpp
 * @brief Monitors ArcDPS log directory for new logs
 *
 * ArcDPS saves logs to:
 * - Documents/Guild Wars 2/addons/arcdps/arcdps.cbtlogs/
 *
 * DO NOT ADD:
 * - Log parsing (belongs in EVTCParser)
 * - Real-time DPS (belongs in ArcDPSRealTime)
 */

#include "ArcDPSWatcher.h"

#include <QDebug>

namespace ArcDPS {

LogWatcher::LogWatcher(QObject *parent)
    : QObject(parent), m_watcher(new QFileSystemWatcher(this)),
      m_parser(new EVTCParser(this)), m_pollTimer(new QTimer(this)) {
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          &LogWatcher::onDirectoryChanged);
  connect(m_pollTimer, &QTimer::timeout, this, &LogWatcher::checkForNewLogs);

  m_logDirectory = defaultLogDirectory();
}

void LogWatcher::setLogDirectory(const QString &path) {
  // Remove old watch
  if (!m_logDirectory.isEmpty() &&
      m_watcher->directories().contains(m_logDirectory)) {
    m_watcher->removePath(m_logDirectory);
  }

  m_logDirectory = path;

  // Add new watch if running
  if (m_pollTimer->isActive() && QDir(path).exists()) {
    m_watcher->addPath(path);
  }
}

QString LogWatcher::defaultLogDirectory() {
  QString docs =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  return QDir(docs).filePath("Guild Wars 2/addons/arcdps/arcdps.cbtlogs");
}

void LogWatcher::start() {
  QDir dir(m_logDirectory);

  if (!dir.exists()) {
    qWarning() << "ArcDPS log directory not found:" << m_logDirectory;
    qInfo() << "Create it or install ArcDPS to enable log watching";
  } else {
    m_watcher->addPath(m_logDirectory);

    // Get list of existing files
    m_knownFiles =
        dir.entryList({"*.evtc", "*.evtc.zip", "*.zevtc"}, QDir::Files);

    qInfo() << "Watching ArcDPS logs:" << m_logDirectory;
    qInfo() << "Found" << m_knownFiles.size() << "existing logs";
  }

  // Also poll periodically (backup for watcher issues)
  m_pollTimer->start(5000);
}

void LogWatcher::stop() {
  m_pollTimer->stop();

  if (!m_logDirectory.isEmpty()) {
    m_watcher->removePath(m_logDirectory);
  }
}

ParsedLog LogWatcher::parseLog(const QString &filePath) {
  ParsedLog log = m_parser->parse(filePath);

  if (log.duration > 0) {
    m_recentLogs.prepend(log);

    // Trim list
    while (m_recentLogs.size() > m_maxRecentLogs) {
      m_recentLogs.removeLast();
    }

    emit logParsed(log);
  }

  return log;
}

void LogWatcher::onDirectoryChanged(const QString &path) {
  Q_UNUSED(path);
  checkForNewLogs();
}

void LogWatcher::checkForNewLogs() {
  QDir dir(m_logDirectory);
  if (!dir.exists())
    return;

  QStringList currentFiles =
      dir.entryList({"*.evtc", "*.evtc.zip", "*.zevtc"}, QDir::Files);

  // Find new files
  for (const QString &file : currentFiles) {
    if (!m_knownFiles.contains(file)) {
      QString fullPath = dir.filePath(file);
      m_knownFiles.append(file);

      qInfo() << "New ArcDPS log detected:" << file;
      emit newLogDetected(fullPath);

      // Auto-parse
      parseLog(fullPath);
    }
  }
}

} // namespace ArcDPS
