#include "ZipExtractor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

QString ZipExtractor::s_lastError;

bool ZipExtractor::extractAll(const QString &zipPath,
                              const QString &outputDir) {
  QuaZip zip(zipPath);
  if (!zip.open(QuaZip::mdUnzip)) {
    s_lastError =
        QStringLiteral("Failed to open ZIP file: %1").arg(zip.getZipError());
    return false;
  }

  QDir outDir(outputDir);
  if (!outDir.mkpath(".")) {
    s_lastError = QStringLiteral("Failed to create output directory");
    zip.close();
    return false;
  }

  QuaZipFile zipFile(&zip);
  for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
    QString fileName = zip.getCurrentFileName();
    if (fileName.isEmpty())
      continue;

    // Handle directory entries
    if (fileName.endsWith('/')) {
      outDir.mkpath(fileName);
      continue;
    }

    // Ensure parent directory exists
    QString filePath = outDir.filePath(fileName);
    QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
      outDir.mkpath(fi.dir().path().mid(outDir.path().length() + 1));
    }

    if (!zipFile.open(QIODevice::ReadOnly)) {
      s_lastError =
          QStringLiteral("Failed to open file in archive: %1").arg(fileName);
      zip.close();
      return false;
    }

    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
      s_lastError = QStringLiteral("Failed to write: %1").arg(filePath);
      zipFile.close();
      zip.close();
      return false;
    }

    // Stream in chunks to handle large files
    constexpr qint64 kChunkSize = 65536;
    char buffer[kChunkSize];
    qint64 bytesRead;
    while ((bytesRead = zipFile.read(buffer, kChunkSize)) > 0) {
      outFile.write(buffer, bytesRead);
    }

    outFile.close();
    zipFile.close();
  }

  zip.close();
  s_lastError.clear();
  return true;
}

QByteArray ZipExtractor::extractFile(const QString &zipPath,
                                     const QString &fileName) {
  QuaZip zip(zipPath);
  if (!zip.open(QuaZip::mdUnzip)) {
    s_lastError =
        QStringLiteral("Failed to open ZIP file: %1").arg(zip.getZipError());
    return {};
  }

  if (!zip.setCurrentFile(fileName)) {
    s_lastError = QStringLiteral("File not found in archive: %1").arg(fileName);
    zip.close();
    return {};
  }

  QuaZipFile zipFile(&zip);
  if (!zipFile.open(QIODevice::ReadOnly)) {
    s_lastError =
        QStringLiteral("Failed to open file in archive: %1").arg(fileName);
    zip.close();
    return {};
  }

  QByteArray data = zipFile.readAll();
  zipFile.close();
  zip.close();
  s_lastError.clear();
  return data;
}

QStringList ZipExtractor::listFiles(const QString &zipPath) {
  QStringList files;

  QuaZip zip(zipPath);
  if (!zip.open(QuaZip::mdUnzip)) {
    s_lastError =
        QStringLiteral("Failed to open ZIP file: %1").arg(zip.getZipError());
    return files;
  }

  for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
    files.append(zip.getCurrentFileName());
  }

  zip.close();
  s_lastError.clear();
  return files;
}

bool ZipExtractor::containsFile(const QString &zipPath,
                                const QString &fileName) {
  QuaZip zip(zipPath);
  if (!zip.open(QuaZip::mdUnzip)) {
    return false;
  }

  bool found = zip.setCurrentFile(fileName);
  zip.close();
  return found;
}

bool ZipExtractor::isValidZipData(const QByteArray &data) {
  // ZIP files start with PK (0x50 0x4B) — the "local file header" signature.
  // This covers .zip, .taco, .aiomt, .bhm — all ZIP-based formats.
  return data.size() >= 4 && data[0] == 'P' && data[1] == 'K';
}

bool ZipExtractor::isValidZipFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  QByteArray header = file.read(4);
  file.close();
  return isValidZipData(header);
}
