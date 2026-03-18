/**
 * @file Logger.cpp
 * @brief File-based logger for debugging
 *
 * Logs to AppData/GW2AIO/logs/gw2aio_YYYY-MM-DD.log
 *
 * DO NOT ADD:
 * - UI code
 * - Settings management
 */

#include "Logger.h"

#include <QDebug>

Logger &Logger::instance() {
  static Logger instance;
  return instance;
}

Logger::Logger() {
  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  m_logDir = QDir(appData).filePath("logs");
}

Logger::~Logger() { shutdown(); }

void Logger::initialize(const QString &logDir) {
  m_logDir = logDir;
  initialize();
}

void Logger::initialize() {
  {
    QMutexLocker locker(&m_mutex);

    // Create log directory
    QDir().mkpath(m_logDir);

    // Create log file with date
    QString date = QDate::currentDate().toString("yyyy-MM-dd");
    m_logFilePath = QDir(m_logDir).filePath(QString("gw2aio_%1.log").arg(date));

    m_logFile = new QFile(m_logFilePath);
    if (m_logFile->open(QIODevice::Append | QIODevice::Text)) {
      m_stream = new QTextStream(m_logFile);

      // Write session header
      *m_stream << "\n"
                << "========================================\n"
                << "GW2 AIO Manager - Session Start\n"
                << "Time: "
                << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
                << "========================================\n";
      m_stream->flush();
    }

    // Install Qt message handler to capture qInfo, qWarning, etc.
    qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext &, const QString &msg) {
          Level level = Level::Debug;
          switch (type) {
          case QtDebugMsg:
            level = Level::Debug;
            break;
          case QtInfoMsg:
            level = Level::Info;
            break;
          case QtWarningMsg:
            level = Level::Warning;
            break;
          case QtCriticalMsg:
            level = Level::Error;
            break;
          case QtFatalMsg:
            level = Level::Critical;
            break;
          }
          Logger::instance().log(level, msg);
        });
  } // mutex released here — rotateLogsIfNeeded() uses qInfo() which re-enters
    // log(), so it must NOT be called under m_mutex

  // Rotate old logs (calls qInfo — safe now because mutex is released)
  rotateLogsIfNeeded();
}

void Logger::shutdown() {
  QMutexLocker locker(&m_mutex);

  if (m_stream) {
    *m_stream << "\n[Session End: "
              << QDateTime::currentDateTime().toString(Qt::ISODate) << "]\n";
    m_stream->flush();
    delete m_stream;
    m_stream = nullptr;
  }

  if (m_logFile) {
    m_logFile->close();
    delete m_logFile;
    m_logFile = nullptr;
  }
}

void Logger::log(Level level, const QString &message, const QString &category) {
  if (level < m_minLevel)
    return;

  QMutexLocker locker(&m_mutex);

  QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
  QString lvl = levelToString(level);
  QString cat = category.isEmpty() ? "" : QString("[%1] ").arg(category);

  QString formatted = QString("[%1] %2 %3%4").arg(timestamp, lvl, cat, message);

  // Console output
  if (m_consoleEnabled) {
    QTextStream out(stdout);
    out << formatted << "\n";
    out.flush();
  }

  // File output
  if (m_fileEnabled && m_stream) {
    *m_stream << formatted << "\n";
    m_stream->flush();
  }

  emit messageLogged(level, message);
}

QString Logger::levelToString(Level level) const {
  switch (level) {
  case Level::Debug:
    return "[DEBUG]  ";
  case Level::Info:
    return "[INFO]   ";
  case Level::Warning:
    return "[WARNING]";
  case Level::Error:
    return "[ERROR]  ";
  case Level::Critical:
    return "[CRITICAL]";
  }
  return "[UNKNOWN]";
}

void Logger::rotateLogsIfNeeded() {
  QDir logDir(m_logDir);
  QStringList logs =
      logDir.entryList({"gw2aio_*.log"}, QDir::Files, QDir::Time);

  // Delete old logs beyond max
  while (logs.size() > m_maxLogFiles) {
    QString oldest = logs.takeLast();
    QFile::remove(logDir.filePath(oldest));
    qInfo() << "Deleted old log:" << oldest;
  }
}
