/**
 * @file GFXManager.cpp
 * @brief GFX Settings Manager implementation
 *
 * Manages GFXSettings.xml files for per-profile graphics settings.
 *
 * DO NOT ADD:
 * - UI code (belongs in GraphicsTabWidget)
 * - Profile management logic (belongs in ProfileManager)
 */

#include "GFXManager.h"

#include <QDateTime>
#include <QDebug>
#include <QDomDocument>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

GFXManager::GFXManager(QObject *parent) : QObject(parent) {
  // GW2 stores GFXSettings in %APPDATA%/Guild Wars 2 (Roaming)
  QString gw2AppData = QDir::homePath() + "/AppData/Roaming/Guild Wars 2/";

  m_gw2GfxPath = gw2AppData + "GFXSettings.Gw2-64.exe.xml";
  m_backupPath = gw2AppData + "GFXSettings.Gw2-64.exe.xml.aio-original";

  // AIO's saved GFX files location
  QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  m_savedGfxPath = dataDir + "/GFXSettings/";
  QDir().mkpath(m_savedGfxPath);

  qInfo() << "GFXManager initialized";
}

GFXManager::GFXManager(const QString &savedGfxDir, QObject *parent)
    : QObject(parent) {
  // GW2 stores GFXSettings in %APPDATA%/Guild Wars 2 (Roaming)
  QString gw2AppData = QDir::homePath() + "/AppData/Roaming/Guild Wars 2/";

  m_gw2GfxPath = gw2AppData + "GFXSettings.Gw2-64.exe.xml";
  m_backupPath = gw2AppData + "GFXSettings.Gw2-64.exe.xml.aio-original";

  // Use injected path, or fall back to default
  if (savedGfxDir.isEmpty()) {
    m_savedGfxPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/GFXSettings/";
  } else {
    m_savedGfxPath = savedGfxDir;
    if (!m_savedGfxPath.endsWith('/')) {
      m_savedGfxPath += '/';
    }
  }
  QDir().mkpath(m_savedGfxPath);

  qInfo() << "GFXManager initialized";
}

bool GFXManager::hasBackup() const { return QFile::exists(m_backupPath); }

bool GFXManager::ensureBackup() {
  if (QFile::exists(m_backupPath)) {
    return true;
  }

  if (!QFile::exists(m_gw2GfxPath)) {
    return true; // Nothing to backup
  }

  qInfo() << "GFXManager: Creating one-time backup of original GFXSettings";
  if (!QFile::copy(m_gw2GfxPath, m_backupPath)) {
    emit error("Failed to backup original GFX settings");
    return false;
  }

  qInfo() << "GFXManager: Backup created:" << m_backupPath;
  return true;
}

// === Presets ===

QStringList GFXManager::presetNames() {
  return {"Best Performance", "Low", "Medium", "High", "Ultra", "Custom"};
}

GfxSettings GFXManager::getPreset(const QString &presetName) {
  GfxSettings s;
  s.presetName = presetName;

  if (presetName == "Best Performance") {
    s.shadows = "off";
    s.reflections = "none";
    s.textureDetail = "low";
    s.shaders = "low";
    s.environment = "low";
    s.animation = "low";
    s.lodDistance = "low";
    s.antiAliasing = "none";
    s.sampling = "subsample";
    s.charModelLimit = "lowest";
    s.charModelQuality = "lowest";
    s.highResCharacter = false;
    s.effectLod = true;
    s.bestTextureFiltering = false;
    s.screenspaceShadows = false;
    s.postProc = "off";
    s.depthBlur = false;
  } else if (presetName == "Low") {
    s.shadows = "low";
    s.reflections = "none";
    s.textureDetail = "low";
    s.shaders = "low";
    s.environment = "low";
    s.animation = "low";
    s.lodDistance = "low";
    s.antiAliasing = "none";
    s.sampling = "native";
    s.charModelLimit = "low";
    s.charModelQuality = "low";
    s.highResCharacter = false;
    s.effectLod = true;
    s.bestTextureFiltering = false;
    s.screenspaceShadows = false;
    s.postProc = "low";
    s.depthBlur = false;
  } else if (presetName == "Medium") {
    s.shadows = "medium";
    s.reflections = "terrain";
    s.textureDetail = "medium";
    s.shaders = "medium";
    s.environment = "medium";
    s.animation = "medium";
    s.lodDistance = "medium";
    s.antiAliasing = "fxaa";
    s.sampling = "native";
    s.charModelLimit = "medium";
    s.charModelQuality = "medium";
    s.highResCharacter = false;
    s.effectLod = false;
    s.bestTextureFiltering = false;
    s.screenspaceShadows = false;
    s.postProc = "medium";
    s.depthBlur = false;
  } else if (presetName == "High") {
    s.shadows = "high";
    s.reflections = "all";
    s.textureDetail = "high";
    s.shaders = "high";
    s.environment = "high";
    s.animation = "high";
    s.lodDistance = "high";
    s.antiAliasing = "smaa_low";
    s.sampling = "native";
    s.charModelLimit = "high";
    s.charModelQuality = "high";
    s.highResCharacter = true;
    s.effectLod = false;
    s.bestTextureFiltering = true;
    s.screenspaceShadows = false;
    s.postProc = "high";
    s.depthBlur = false;
  } else if (presetName == "Ultra") {
    s.shadows = "ultra";
    s.reflections = "all";
    s.textureDetail = "high";
    s.shaders = "high";
    s.environment = "high";
    s.animation = "high";
    s.lodDistance = "ultra";
    s.antiAliasing = "smaa_high";
    s.sampling = "supersample";
    s.charModelLimit = "highest";
    s.charModelQuality = "highest";
    s.highResCharacter = true;
    s.effectLod = false;
    s.bestTextureFiltering = true;
    s.screenspaceShadows = true;
    s.postProc = "high";
    s.depthBlur = true;
  }
  // Custom: use defaults (High-ish)

  return s;
}

// === XML Parsing ===

GfxSettings GFXManager::parseGfxFile(const QString &path) {
  GfxSettings settings;

  // Debug logging
  QFile debugLog(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/gfx_debug.log");
  if (!debugLog.open(QIODevice::WriteOnly | QIODevice::Append |
                     QIODevice::Text))
    return settings;
  QTextStream log(&debugLog);
  log << "\n=== GFXManager::parseGfxFile() "
      << QDateTime::currentDateTime().toString() << " ===\n";
  log << "  Path: " << path << "\n";

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    log << "  ERROR: Cannot open file!\n";
    debugLog.close();
    qWarning() << "GFXManager: Cannot open file:" << path;
    return settings;
  }

  log << "  File opened successfully, size: " << file.size() << " bytes\n";

  QXmlStreamReader xml(&file);

  while (!xml.atEnd() && !xml.hasError()) {
    QXmlStreamReader::TokenType token = xml.readNext();
    if (token != QXmlStreamReader::StartElement)
      continue;

    QString name = xml.name().toString();

    // Skip root element GSA_SDK (it has children, not text)
    if (name == "GSA_SDK")
      continue;

    QString value = xml.readElementText();

    // Display
    if (name == "screenMode")
      settings.screenMode = value;
    else if (name == "width")
      settings.width = value.toInt();
    else if (name == "height")
      settings.height = value.toInt();
    else if (name == "refreshRate")
      settings.refreshRate = value.toInt();
    else if (name == "frameLimit")
      settings.frameLimit = value;
    else if (name == "gamma")
      settings.gamma = value.toDouble();
    else if (name == "verticalSync")
      settings.verticalSync = (value == "true");
    else if (name == "dpiScaling")
      settings.dpiScaling = (value == "true");
    // Quality
    else if (name == "shadows") {
      settings.shadows = value;
      log << "  PARSED shadows: " << value << "\n";
    } else if (name == "reflections")
      settings.reflections = value;
    else if (name == "textureDetail")
      settings.textureDetail = value;
    else if (name == "shaders")
      settings.shaders = value;
    else if (name == "environment")
      settings.environment = value;
    else if (name == "animation")
      settings.animation = value;
    else if (name == "lodDistance")
      settings.lodDistance = value;
    else if (name == "antiAliasing")
      settings.antiAliasing = value;
    else if (name == "sampling")
      settings.sampling = value;
    // Characters
    else if (name == "charModelLimit")
      settings.charModelLimit = value;
    else if (name == "charModelQuality")
      settings.charModelQuality = value;
    else if (name == "highResCharacter")
      settings.highResCharacter = (value == "true");
    else if (name == "effectLod")
      settings.effectLod = (value == "true");
    // Advanced
    else if (name == "bestTextureFiltering")
      settings.bestTextureFiltering = (value == "true");
    else if (name == "screenspaceShadows")
      settings.screenspaceShadows = (value == "true");
    else if (name == "postProc")
      settings.postProc = value;
    else if (name == "depthBlur")
      settings.depthBlur = (value == "true");
    // AIO extension: save preset name
    else if (name == "aioPreset") {
      settings.presetName = value;
      log << "  PARSED aioPreset: " << value << "\n";
    }
  }

  if (xml.hasError()) {
    log << "  XML parse error: " << xml.errorString() << "\n";
    qWarning() << "GFXManager: XML parse error:" << xml.errorString();
  }

  file.close();
  // Only default to Custom if no preset was found in file
  if (settings.presetName.isEmpty()) {
    settings.presetName = "Custom";
  }

  log << "  Final parsed values:\n";
  log << "    shadows: " << settings.shadows << "\n";
  log << "    presetName: " << settings.presetName << "\n";
  debugLog.close();
  return settings;
}

bool GFXManager::writeGfxFile(const QString &path, const GfxSettings &s) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning() << "GFXManager: Cannot write file:" << path;
    return false;
  }

  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("GSA_SDK");

  // Display
  xml.writeTextElement("screenMode", s.screenMode);
  xml.writeTextElement("width", QString::number(s.width));
  xml.writeTextElement("height", QString::number(s.height));
  xml.writeTextElement("refreshRate", QString::number(s.refreshRate));
  xml.writeTextElement("frameLimit", s.frameLimit);
  xml.writeTextElement("gamma", QString::number(s.gamma, 'f', 2));
  xml.writeTextElement("verticalSync", s.verticalSync ? "true" : "false");
  xml.writeTextElement("dpiScaling", s.dpiScaling ? "true" : "false");

  // Quality
  xml.writeTextElement("shadows", s.shadows);
  xml.writeTextElement("reflections", s.reflections);
  xml.writeTextElement("textureDetail", s.textureDetail);
  xml.writeTextElement("shaders", s.shaders);
  xml.writeTextElement("environment", s.environment);
  xml.writeTextElement("animation", s.animation);
  xml.writeTextElement("lodDistance", s.lodDistance);
  xml.writeTextElement("antiAliasing", s.antiAliasing);
  xml.writeTextElement("sampling", s.sampling);

  // Characters
  xml.writeTextElement("charModelLimit", s.charModelLimit);
  xml.writeTextElement("charModelQuality", s.charModelQuality);
  xml.writeTextElement("highResCharacter",
                       s.highResCharacter ? "true" : "false");
  xml.writeTextElement("effectLod", s.effectLod ? "true" : "false");

  // Advanced
  xml.writeTextElement("bestTextureFiltering",
                       s.bestTextureFiltering ? "true" : "false");
  xml.writeTextElement("screenspaceShadows",
                       s.screenspaceShadows ? "true" : "false");
  xml.writeTextElement("postProc", s.postProc);
  xml.writeTextElement("depthBlur", s.depthBlur ? "true" : "false");

  // AIO extension: save preset name for restoration
  xml.writeTextElement("aioPreset", s.presetName);

  xml.writeEndElement(); // GSA_SDK
  xml.writeEndDocument();

  file.close();
  qInfo() << "GFXManager: Wrote settings to:" << path
          << "preset:" << s.presetName;
  return true;
}

bool GFXManager::applyGfxSettings(const QString &gfxPath) {
  qInfo() << "=== GFXManager::applyGfxSettings() ===";
  qInfo() << "  Source:" << gfxPath;
  qInfo() << "  Target:" << m_gw2GfxPath;

  if (gfxPath.isEmpty() || !QFile::exists(gfxPath)) {
    qWarning() << "GFXManager: Source GFX file not found:" << gfxPath;
    emit error("GFX settings file not found");
    return false;
  }

  // 1. Parse our saved settings (simplified format)
  GfxSettings settings = parseGfxFile(gfxPath);
  qInfo() << "  Loaded settings: shadows=" << settings.shadows
          << "presetName=" << settings.presetName;

  // 2. Ensure backup of original GW2 file
  if (!ensureBackup()) {
    return false;
  }

  // 3. Read GW2's native file
  QFile nativeFile(m_gw2GfxPath);
  if (!nativeFile.exists()) {
    // No native file - try restoring from backup
    if (QFile::exists(m_backupPath)) {
      QFile::copy(m_backupPath, m_gw2GfxPath);
    } else {
      // Create a template GFX file - GW2 may not have run yet
      // Format matches actual GW2 GFXSettings.Gw2-64.exe.xml format
      qInfo() << "GFXManager: Creating template GFX file (game hasn't run yet)";
      QString templateXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<GSA_SDK>
    <GAMESETTINGS>
        <RESOLUTION Width="1920" Height="1080" RefreshRate="60"/>
        <OPTION Name="screenMode" Registered="True" Type="Enum" Value="windowed"/>
        <OPTION Name="shadows" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="reflections" Registered="True" Type="Enum" Value="terrain"/>
        <OPTION Name="antiAliasing" Registered="True" Type="Enum" Value="fxaa"/>
        <OPTION Name="textureDetail" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="shaders" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="environment" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="animation" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="lodDistance" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="charModelLimit" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="charModelQuality" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="postProc" Registered="True" Type="Enum" Value="high"/>
        <OPTION Name="sampling" Registered="True" Type="Enum" Value="native"/>
        <OPTION Name="frameLimit" Registered="True" Type="Enum" Value="60"/>
        <OPTION Name="gamma" Registered="True" Type="Float" Value="1"/>
        <OPTION Name="verticalSync" Registered="True" Type="Bool" Value="true"/>
        <OPTION Name="dpiScaling" Registered="True" Type="Bool" Value="false"/>
        <OPTION Name="effectLod" Registered="True" Type="Bool" Value="false"/>
        <OPTION Name="highResCharacter" Registered="True" Type="Bool" Value="true"/>
        <OPTION Name="bestTextureFiltering" Registered="True" Type="Bool" Value="true"/>
        <OPTION Name="depthBlur" Registered="True" Type="Bool" Value="false"/>
        <OPTION Name="screenspaceShadows" Registered="True" Type="Bool" Value="true"/>
    </GAMESETTINGS>
</GSA_SDK>
)";
      // Ensure directory exists
      QDir().mkpath(QFileInfo(m_gw2GfxPath).path());
      QFile templateFile(m_gw2GfxPath);
      if (!templateFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not create GFX template file");
        return false;
      }
      templateFile.write(templateXml.toUtf8());
      templateFile.close();
      qInfo() << "GFXManager: Template GFX file created at" << m_gw2GfxPath;
    }
  }

  if (!nativeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    emit error("Could not read GW2 GFX file");
    return false;
  }

  // 4. Parse native file with QDomDocument
  QDomDocument doc;
  QString errorMsg;
  int errorLine, errorCol;
  if (!doc.setContent(&nativeFile, &errorMsg, &errorLine, &errorCol)) {
    nativeFile.close();
    qWarning() << "GFXManager: Failed to parse native GFX XML:" << errorMsg
               << "at line" << errorLine << "col" << errorCol;
    emit error("Failed to parse GW2 GFX settings");
    return false;
  }
  nativeFile.close();

  // 5. Find GAMESETTINGS and update OPTION values
  QDomNodeList options = doc.elementsByTagName("OPTION");
  for (int i = 0; i < options.size(); i++) {
    QDomElement option = options.at(i).toElement();
    if (option.isNull())
      continue;

    QString name = option.attribute("Name");

    // Map our settings to native OPTION elements
    if (name == "shadows")
      option.setAttribute("Value", settings.shadows);
    else if (name == "reflections")
      option.setAttribute("Value", settings.reflections);
    else if (name == "screenMode")
      option.setAttribute("Value", settings.screenMode);
    else if (name == "antiAliasing")
      option.setAttribute("Value", settings.antiAliasing);
    else if (name == "textureDetail")
      option.setAttribute("Value", settings.textureDetail);
    else if (name == "shaders")
      option.setAttribute("Value", settings.shaders);
    else if (name == "environment")
      option.setAttribute("Value", settings.environment);
    else if (name == "animation")
      option.setAttribute("Value", settings.animation);
    else if (name == "lodDistance")
      option.setAttribute("Value", settings.lodDistance);
    else if (name == "charModelLimit")
      option.setAttribute("Value", settings.charModelLimit);
    else if (name == "charModelQuality")
      option.setAttribute("Value", settings.charModelQuality);
    else if (name == "postProc")
      option.setAttribute("Value", settings.postProc);
    else if (name == "sampling")
      option.setAttribute("Value", settings.sampling);
    else if (name == "frameLimit")
      option.setAttribute("Value", settings.frameLimit);
    else if (name == "gamma")
      option.setAttribute("Value", QString::number(settings.gamma, 'f', 5));
    else if (name == "verticalSync")
      option.setAttribute("Value", settings.verticalSync ? "true" : "false");
    else if (name == "dpiScaling")
      option.setAttribute("Value", settings.dpiScaling ? "true" : "false");
    else if (name == "effectLod")
      option.setAttribute("Value", settings.effectLod ? "true" : "false");
    else if (name == "highResCharacter")
      option.setAttribute("Value",
                          settings.highResCharacter ? "true" : "false");
    else if (name == "bestTextureFiltering")
      option.setAttribute("Value",
                          settings.bestTextureFiltering ? "true" : "false");
    else if (name == "depthBlur")
      option.setAttribute("Value", settings.depthBlur ? "true" : "false");
    else if (name == "screenspaceShadows")
      option.setAttribute("Value",
                          settings.screenspaceShadows ? "true" : "false");
  }

  // 6. Update RESOLUTION element
  QDomNodeList resolutions = doc.elementsByTagName("RESOLUTION");
  if (resolutions.size() > 0) {
    QDomElement resolution = resolutions.at(0).toElement();
    resolution.setAttribute("Width", settings.width);
    resolution.setAttribute("Height", settings.height);
    resolution.setAttribute("RefreshRate", settings.refreshRate);
  }

  // 7. Write back to file
  if (!nativeFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    emit error("Could not write GW2 GFX file");
    return false;
  }

  QTextStream stream(&nativeFile);
  stream << doc.toString(4); // 4-space indentation
  nativeFile.close();

  QString profileName = QFileInfo(gfxPath).baseName();
  qInfo() << "GFXManager: Successfully applied GFX settings for:"
          << profileName;
  emit applied(profileName);
  return true;
}
