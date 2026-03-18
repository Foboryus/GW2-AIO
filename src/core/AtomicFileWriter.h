#pragma once

/**
 * @brief Atomic JSON file writer with backup rotation
 *
 * Shared utility for safe file writes across the application.
 * Pattern: write .tmp → validate → rotate .bak → rename.
 *
 * DO NOT ADD:
 * - Data-specific logic (belongs in managers)
 * - Path resolution (belongs in StorageBackend)
 */

#include <QString>

class QJsonObject;

class AtomicFileWriter {
public:
  /**
   * @brief Atomically write a JSON object to a file
   *
   * 1. Writes data to filePath.tmp
   * 2. Re-reads and validates the temp file
   * 3. Rotates: current → .bak, temp → current
   * 4. On failure: rolls back and removes temp
   *
   * @param filePath Target file path
   * @param data JSON object to write
   * @return true if write succeeded, false on any failure
   */
  static bool writeJson(const QString &filePath, const QJsonObject &data);

  /**
   * @brief Atomically copy a binary file with backup rotation
   *
   * 1. Copies source to dest.tmp
   * 2. Validates temp file size matches source
   * 3. Rotates: current dest → .bak, temp → dest
   * 4. On failure: rolls back and removes temp
   *
   * @param source Source file path (must exist)
   * @param dest Destination file path
   * @return true if copy succeeded, false on any failure
   */
  static bool copyBinary(const QString &source, const QString &dest);
};
