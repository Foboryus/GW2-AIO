#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * @brief ZIP archive extraction utility
 *
 * Uses QuaZip (via vcpkg) for robust ZIP extraction.
 * Supports .zip, .taco, .bhm archives.
 *
 * DO NOT ADD:
 * - Inline implementations (use ZipExtractor.cpp)
 */
class ZipExtractor {
public:
  /**
   * @brief Extract all files from a ZIP archive
   * @param zipPath Path to the .zip/.taco/.bhm file
   * @param outputDir Directory to extract to
   * @return true if successful
   */
  static bool extractAll(const QString &zipPath, const QString &outputDir);

  /**
   * @brief Extract a single file from a ZIP archive
   * @param zipPath Path to the archive
   * @param fileName Name of the file inside the archive
   * @return File contents, or empty on failure
   */
  static QByteArray extractFile(const QString &zipPath,
                                const QString &fileName);

  /**
   * @brief List files in a ZIP archive
   */
  static QStringList listFiles(const QString &zipPath);

  /**
   * @brief Check if a file exists in the archive
   */
  static bool containsFile(const QString &zipPath, const QString &fileName);

  /**
   * @brief Check if raw data has valid ZIP magic bytes (PK header)
   * @param data The raw bytes to check
   * @return true if data starts with PK (0x50 0x4B)
   */
  static bool isValidZipData(const QByteArray &data);

  /**
   * @brief Check if a file on disk is a valid ZIP archive
   * @param path Path to .taco/.zip/.aiomt/.bhm file
   * @return true if file exists and has valid PK header
   */
  static bool isValidZipFile(const QString &path);

  /**
   * @brief Get last error message
   */
  static QString lastError() { return s_lastError; }

private:
  static QString s_lastError;
};
