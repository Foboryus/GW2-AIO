/**
 * @file 09_Misc.h
 * @brief QSS Section 9: QTextBrowser (new) and QToolTip
 *
 * NEW: QTextBrowser rule for About dialog and any future text browsers
 * FIX: QToolTip font-size changed from hardcoded 12px
 *      to themable {{layout.fontSize.hint}}px
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString misc() {
  return QStringLiteral(R"QSS(

/* ================================================================
   TEXT BROWSER
   ================================================================ */

QTextBrowser {
    background: {{widget.list.bg}};
    color: {{text.primary}};
    border: none;
    padding: 8px;
    border-radius: 4px;
    font-size: {{layout.fontSize.normal}}px;
}

/* ================================================================
   TOOLTIP
   ================================================================ */

QToolTip {
    background-color: {{window.surface}};
    color: {{text.primary}};
    border: 1px solid {{container.bg}};
    border-radius: {{layout.borderRadius}}px;
    padding: 8px 12px;
    font-size: {{layout.fontSize.hint}}px;
}

)QSS");
}

} // namespace QssTemplate
