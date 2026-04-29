#pragma once

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>


#include "ArcDPSModels.h"

namespace ArcDPS {

/**
 * @brief Parses ArcDPS EVTC log files
 *
 * DO NOT ADD:
 * - Inline implementations (use EVTCParser.cpp)
 */
class EVTCParser : public QObject {
  Q_OBJECT

public:
  explicit EVTCParser(QObject *parent = nullptr);

  /**
   * @brief Parse an EVTC or EVTC.ZIP file
   */
  ParsedLog parse(const QString &filePath);

  /**
   * @brief Get last error
   */
  QString lastError() const { return m_lastError; }

private:
  bool readHeader(QDataStream &stream, ParsedLog &log);
  bool readAgents(QDataStream &stream, ParsedLog &log);
  bool readSkills(QDataStream &stream, ParsedLog &log);
  bool readEvents(QDataStream &stream, ParsedLog &log);
  void computeStats(ParsedLog &log);

  QString m_lastError;
};

} // namespace ArcDPS
