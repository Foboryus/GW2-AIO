#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QStandardPaths>
#include <QTextStream>


/**
 * @brief File-based logger for debugging
 *
 * Logs to AppData/GW2AIO/logs/gw2aio_YYYY-MM-DD.log
 *
 * DO NOT ADD:
 * - Inline implementations (use Logger.cpp)
 */
class Logger : public QObject {
  Q_OBJECT

public:
  enum class Level { Debug, Info, Warning, Error, Critical };
  Q_ENUM(Level)

  static Logger &instance();

  /**
   * @brief Initialize logging with custom directory
   */
  void initialize(const QString &logDir);

  /**
   * @brief Initialize logging in default directory
   */
  void initialize();

  /**
   * @brief Set a custom file prefix for the log filename.
   * Must be called BEFORE initialize(). Defaults to "gw2aio".
   * Example: setFilePrefix("gw2aio_radial_2885f7f4")
   *   → log file: gw2aio_radial_2885f7f4_2026-04-28.log
   */
  void setFilePrefix(const QString &prefix) { m_filePrefix = prefix; }

  /**
   * @brief Alias for setConsoleEnabled
   */
  void setConsoleOutput(bool enabled) { m_consoleEnabled = enabled; }

  /**
   * @brief Shutdown logging
   */
  void shutdown();

  /**
   * @brief Log a message
   */
  void log(Level level, const QString &message,
           const QString &category = QString());

  /**
   * @brief Set minimum log level
   */
  void setMinLevel(Level level) { m_minLevel = level; }

  /**
   * @brief Enable/disable console output
   */
  void setConsoleEnabled(bool enabled) { m_consoleEnabled = enabled; }

  /**
   * @brief Enable/disable file output
   */
  void setFileEnabled(bool enabled) { m_fileEnabled = enabled; }

  /**
   * @brief Get log file path
   */
  QString logFilePath() const { return m_logFilePath; }

  // Convenience methods
  void debug(const QString &msg, const QString &cat = QString()) {
    log(Level::Debug, msg, cat);
  }
  void info(const QString &msg, const QString &cat = QString()) {
    log(Level::Info, msg, cat);
  }
  void warning(const QString &msg, const QString &cat = QString()) {
    log(Level::Warning, msg, cat);
  }
  void error(const QString &msg, const QString &cat = QString()) {
    log(Level::Error, msg, cat);
  }

signals:
  void messageLogged(Level level, const QString &message);

private:
  Logger();
  ~Logger();

  QString levelToString(Level level) const;
  void rotateLogsIfNeeded();

  QFile *m_logFile = nullptr;
  QTextStream *m_stream = nullptr;
  QString m_logFilePath;
  QString m_logDir;
  QString m_filePrefix = QStringLiteral("gw2aio");

  Level m_minLevel = Level::Debug;
  bool m_consoleEnabled = true;
  bool m_fileEnabled = true;
  int m_maxLogFiles = 7; // Keep 7 days of logs

  QMutex m_mutex;
};

// Global logging macros
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARN(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
