/**
 * @file AtomicFileWriter.cpp
 * @brief Atomic JSON file writer implementation
 *
 * Write → validate → rotate pattern prevents data corruption.
 * Used by ProfileManager, HotkeysTabWidget, and any future
 * component that needs safe file persistence.
 *
 * DO NOT ADD:
 * - Data-specific logic (belongs in managers)
 * - Path resolution (belongs in StorageBackend)
 */

#include "AtomicFileWriter.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool AtomicFileWriter::writeJson(const QString &filePath,
                                 const QJsonObject &data) {
  QString tempPath = filePath + ".tmp";
  QString backupPath = filePath + ".bak";

  // Step 1: Write to temp file
  QFile tempFile(tempPath);
  if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "AtomicFileWriter: Failed to create temp file:" << tempPath;
    return false;
  }
  QByteArray jsonData = QJsonDocument(data).toJson();
  if (tempFile.write(jsonData) != jsonData.size()) {
    qWarning() << "AtomicFileWriter: Failed to write all data to temp file";
    tempFile.close();
    QFile::remove(tempPath);
    return false;
  }
  tempFile.close();

  // Step 2: Validate temp file (re-read and parse)
  QFile validateFile(tempPath);
  if (!validateFile.open(QIODevice::ReadOnly)) {
    qWarning() << "AtomicFileWriter: Failed to validate temp file";
    QFile::remove(tempPath);
    return false;
  }
  QJsonParseError parseError;
  QJsonDocument::fromJson(validateFile.readAll(), &parseError);
  validateFile.close();
  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "AtomicFileWriter: Temp file validation failed:"
               << parseError.errorString();
    QFile::remove(tempPath);
    return false;
  }

  // Step 3: Rotate files (atomic renames)
  QFile::remove(backupPath);                // Remove old backup
  QFile::rename(filePath, backupPath);      // Current -> Backup
  if (!QFile::rename(tempPath, filePath)) { // Temp -> Current
    qWarning() << "AtomicFileWriter: Failed to rename temp file to:"
               << filePath;
    // Try to restore backup
    QFile::rename(backupPath, filePath);
    return false;
  }

  return true;
}

bool AtomicFileWriter::copyBinary(const QString &source, const QString &dest) {
  QString tempPath = dest + ".tmp";
  QString backupPath = dest + ".bak";

  // Validate source exists and is readable
  QFileInfo sourceInfo(source);
  if (!sourceInfo.exists() || sourceInfo.size() == 0) {
    qWarning() << "AtomicFileWriter::copyBinary: Source file invalid:"
               << source;
    return false;
  }

  // Step 1: Copy source to temp file
  QFile::remove(tempPath); // Clean up any previous temp
  if (!QFile::copy(source, tempPath)) {
    qWarning() << "AtomicFileWriter::copyBinary: Failed to copy to temp:"
               << source << "->" << tempPath;
    return false;
  }

  // Step 2: Validate temp file size matches source
  QFileInfo tempInfo(tempPath);
  if (tempInfo.size() != sourceInfo.size()) {
    qWarning() << "AtomicFileWriter::copyBinary: Size mismatch! Source:"
               << sourceInfo.size() << "Temp:" << tempInfo.size();
    QFile::remove(tempPath);
    return false;
  }

  // Step 3: Rotate files (atomic renames)
  QFile::remove(backupPath);            // Remove old backup
  QFile::rename(dest, backupPath);      // Current -> Backup
  if (!QFile::rename(tempPath, dest)) { // Temp -> Current
    qWarning() << "AtomicFileWriter::copyBinary: Failed to rename temp to:"
               << dest;
    // Try to restore backup
    QFile::rename(backupPath, dest);
    QFile::remove(tempPath);
    return false;
  }

  return true;
}
