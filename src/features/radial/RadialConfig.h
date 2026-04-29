#pragma once

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>


#include "RadialMenu.h"

/**
 * @brief Manages radial menu configurations
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialConfig.cpp)
 */
class RadialConfig : public QObject {
  Q_OBJECT

public:
  explicit RadialConfig(QObject *parent = nullptr);

  /**
   * @brief Load menus from config file
   */
  void loadConfig();

  /**
   * @brief Save menus to config file
   */
  void saveConfig();

  /**
   * @brief Get all configured menus
   */
  QList<RadialMenu> &menus() { return m_menus; }
  const QList<RadialMenu> &menus() const { return m_menus; }

  /**
   * @brief Get menu by ID
   */
  RadialMenu *getMenu(const QString &id);

  /**
   * @brief Add a new menu
   */
  void addMenu(const RadialMenu &menu);

  /**
   * @brief Remove menu by ID
   */
  void removeMenu(const QString &id);

  /**
   * @brief Reset to default menus
   */
  void resetToDefaults();

signals:
  void menusChanged();

private:
  QString configPath() const;

  QList<RadialMenu> m_menus;
};
