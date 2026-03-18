#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>


/**
 * @brief Localization/translation manager
 *
 * DO NOT ADD:
 * - Inline implementations (use Localization.cpp)
 */
class Localization : public QObject {
  Q_OBJECT

public:
  enum class Language { English, German, French, Spanish };
  Q_ENUM(Language)

  static Localization &instance();

  /**
   * @brief Load a language
   */
  bool loadLanguage(Language lang);
  bool loadLanguage(const QString &langCode);

  /**
   * @brief Get current language
   */
  Language currentLanguage() const { return m_currentLang; }
  QString currentLanguageCode() const;

  /**
   * @brief Translate a string
   */
  QString translate(const QString &key) const;
  QString tr(const QString &key) const { return translate(key); }

  /**
   * @brief Get available languages
   */
  QStringList availableLanguages() const;

signals:
  void languageChanged(Language lang);

private:
  Localization();
  void loadBuiltinStrings();

  Language m_currentLang = Language::English;
  QMap<QString, QString> m_strings;

  // Built-in strings for all languages
  QMap<Language, QMap<QString, QString>> m_builtinStrings;
};

// Convenience macro
#define TR(key) Localization::instance().tr(key)
