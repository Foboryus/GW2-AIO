/**
 * @file Localization.cpp
 * @brief Localization/translation manager
 *
 * DO NOT ADD:
 * - UI code
 * - Hard-coded paths
 */

#include "Localization.h"

Localization &Localization::instance() {
  static Localization instance;
  return instance;
}

Localization::Localization() {
  loadBuiltinStrings();
  loadLanguage(Language::English);
}

void Localization::loadBuiltinStrings() {
  // English (base)
  m_builtinStrings[Language::English] = {
      {"app.name", "GW2 AIO Manager"},
      {"app.version", "Version"},
      {"menu.file", "File"},
      {"menu.settings", "Settings"},
      {"menu.help", "Help"},
      {"menu.quit", "Quit"},
      {"settings.general", "General"},
      {"settings.radial", "Radial Menus"},
      {"settings.dps", "DPS Tracker"},
      {"settings.markers", "Markers"},
      {"settings.modules", "Modules"},
      {"settings.advanced", "Advanced"},
      {"settings.save", "Save"},
      {"settings.reset", "Reset to Defaults"},
      {"radial.mounts", "Mounts"},
      {"radial.novelties", "Novelties"},
      {"radial.markers", "Squad Markers"},
      {"dps.title", "DPS Tracker"},
      {"dps.self", "Self DPS"},
      {"dps.group", "Group DPS"},
      {"markers.title", "Marker System"},
      {"markers.enable", "Enable Markers"},
      {"modules.title", "Blish Modules"},
      {"modules.enable", "Enable Module Support"},
      {"wizard.welcome", "Welcome to GW2 AIO Manager"},
      {"wizard.next", "Next"},
      {"wizard.back", "Back"},
      {"wizard.finish", "Finish"},
      {"wizard.gw2path", "Locate Guild Wars 2"},
      {"wizard.features", "Select Features"},
      {"wizard.complete", "Setup Complete!"},
      {"error.title", "Error"},
      {"error.gw2notfound", "Guild Wars 2 not found"},
      {"update.available", "Update Available"},
      {"update.download", "Download"},
      {"update.later", "Later"},
  };

  // German
  m_builtinStrings[Language::German] = {
      {"app.name", "GW2 AIO Manager"},
      {"menu.file", "Datei"},
      {"menu.settings", "Einstellungen"},
      {"menu.help", "Hilfe"},
      {"menu.quit", "Beenden"},
      {"settings.general", "Allgemein"},
      {"settings.save", "Speichern"},
      {"settings.reset", "Zurücksetzen"},
      {"radial.mounts", "Reittiere"},
      {"radial.novelties", "Spielzeug"},
      {"radial.markers", "Trupp-Marker"},
      {"dps.title", "DPS-Tracker"},
      {"wizard.welcome", "Willkommen beim GW2 AIO Manager"},
      {"wizard.next", "Weiter"},
      {"wizard.back", "Zurück"},
      {"wizard.finish", "Fertig"},
      {"error.title", "Fehler"},
  };

  // French
  m_builtinStrings[Language::French] = {
      {"app.name", "GW2 AIO Manager"},
      {"menu.file", "Fichier"},
      {"menu.settings", "Paramètres"},
      {"menu.help", "Aide"},
      {"menu.quit", "Quitter"},
      {"settings.general", "Général"},
      {"settings.save", "Sauvegarder"},
      {"radial.mounts", "Montures"},
      {"radial.novelties", "Gadgets"},
      {"wizard.welcome", "Bienvenue dans GW2 AIO Manager"},
      {"wizard.next", "Suivant"},
      {"wizard.back", "Retour"},
      {"wizard.finish", "Terminer"},
      {"error.title", "Erreur"},
  };

  // Spanish
  m_builtinStrings[Language::Spanish] = {
      {"app.name", "GW2 AIO Manager"},
      {"menu.file", "Archivo"},
      {"menu.settings", "Configuración"},
      {"menu.help", "Ayuda"},
      {"menu.quit", "Salir"},
      {"settings.general", "General"},
      {"settings.save", "Guardar"},
      {"radial.mounts", "Monturas"},
      {"radial.novelties", "Novedades"},
      {"wizard.welcome", "Bienvenido a GW2 AIO Manager"},
      {"wizard.next", "Siguiente"},
      {"wizard.back", "Atrás"},
      {"wizard.finish", "Finalizar"},
      {"error.title", "Error"},
  };
}

bool Localization::loadLanguage(Language lang) {
  m_currentLang = lang;

  // Start with English base
  m_strings = m_builtinStrings[Language::English];

  // Override with target language
  if (lang != Language::English && m_builtinStrings.contains(lang)) {
    for (auto it = m_builtinStrings[lang].begin();
         it != m_builtinStrings[lang].end(); ++it) {
      m_strings[it.key()] = it.value();
    }
  }

  emit languageChanged(lang);
  return true;
}

bool Localization::loadLanguage(const QString &langCode) {
  if (langCode == "de")
    return loadLanguage(Language::German);
  if (langCode == "fr")
    return loadLanguage(Language::French);
  if (langCode == "es")
    return loadLanguage(Language::Spanish);
  return loadLanguage(Language::English);
}

QString Localization::currentLanguageCode() const {
  switch (m_currentLang) {
  case Language::German:
    return "de";
  case Language::French:
    return "fr";
  case Language::Spanish:
    return "es";
  default:
    return "en";
  }
}

QString Localization::translate(const QString &key) const {
  return m_strings.value(key, key);
}

QStringList Localization::availableLanguages() const {
  return {"en", "de", "fr", "es"};
}
