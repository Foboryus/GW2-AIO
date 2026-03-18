/**
 * @file 02_Navigation.h
 * @brief QSS Section 2: Navbar and page header strip
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString navigation() {
  return QStringLiteral(R"QSS(

/* ================================================================
   NAVBAR & PAGE HEADER
   ================================================================ */

#navBar {
    background-color: {{titlebar.bg}};
    border-radius: {{layout.popupBorderRadius}}px;
}

/* --- Content pages area --- */
QStackedWidget#pages,
QStackedWidget#pages > QWidget {
    background-color: {{container.bg}};
    border-radius: {{layout.popupBorderRadius}}px;
}

/* --- Page Header Strip --- */
QWidget[role="pageHeader"] {
    background: {{widget.pageHeader.bg}};
    border: 1px solid {{widget.pageHeader.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: 12px 16px;
}
QLabel[role="pageTitle"] {
    font-size: {{layout.fontSize.pageTitle}}px;
    font-weight: bold;
    color: {{widget.pageHeader.text}};
    background: transparent;
    border: none;
    padding: 0px;
    margin: 0px;
}

)QSS");
}

} // namespace QssTemplate
