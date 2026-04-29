/**
 * @file RadialConfig.cpp
 * @brief Manages radial menu configurations
 *
 * DO NOT ADD:
 * - UI code (belongs in HotkeyEditor)
 * - Menu rendering (belongs in RadialWidget)
 */

#include "RadialConfig.h"

#include <QDebug>
#include <algorithm>

RadialConfig::RadialConfig(QObject *parent) : QObject(parent) { loadConfig(); }

void RadialConfig::loadConfig() {
  QString path = configPath();
  QFile file(path);

  if (!file.exists()) {
    resetToDefaults();
    saveConfig();
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open radial config:" << path;
    resetToDefaults();
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();

  m_menus.clear();
  for (const auto &menuJson : doc.array()) {
    m_menus.append(RadialMenu::fromJson(menuJson.toObject()));
  }

  if (m_menus.isEmpty()) {
    resetToDefaults();
  }

  emit menusChanged();
}

void RadialConfig::saveConfig() {
  QString path = configPath();
  QDir dir = QFileInfo(path).dir();
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to save radial config:" << path;
    return;
  }

  QJsonArray menusJson;
  for (const auto &menu : m_menus) {
    menusJson.append(menu.toJson());
  }

  file.write(QJsonDocument(menusJson).toJson(QJsonDocument::Indented));
  file.close();
}

RadialMenu *RadialConfig::getMenu(const QString &id) {
  for (auto &menu : m_menus) {
    if (menu.id == id) {
      return &menu;
    }
  }
  return nullptr;
}

void RadialConfig::addMenu(const RadialMenu &menu) {
  m_menus.append(menu);
  saveConfig();
  emit menusChanged();
}

void RadialConfig::removeMenu(const QString &id) {
  m_menus.erase(
      std::remove_if(m_menus.begin(), m_menus.end(),
                     [&id](const RadialMenu &m) { return m.id == id; }),
      m_menus.end());
  saveConfig();
  emit menusChanged();
}

void RadialConfig::resetToDefaults() {
  m_menus.clear();
  m_menus.append(DefaultMenus::mounts());
  m_menus.append(DefaultMenus::novelties());
  m_menus.append(DefaultMenus::markers());
  emit menusChanged();
}

QString RadialConfig::configPath() const {
  QString configDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(configDir).filePath("radial_menus.json");
}
