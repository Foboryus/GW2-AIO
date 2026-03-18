/**
 * @file 01_Base.h
 * @brief QSS Section 1: Base widget defaults (QWidget, QMainWindow)
 *
 * Part of the modular QSS template system. Each file returns a
 * QStringLiteral fragment; ThemeManager concatenates them in order.
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString base() {
  return QStringLiteral(R"QSS(

/* ================================================================
   BASE WIDGETS
   ================================================================ */

QWidget {
    background: {{window.bg}};
    color: {{text.primary}};
    font-family: 'Segoe UI', Arial, sans-serif;
}

QMainWindow {
    background: transparent;
}

)QSS");
}

} // namespace QssTemplate
