/**
 * @file 06_WindowChrome.h
 * @brief QSS Section 6: Title bar buttons and navigation sidebar buttons
 *
 * FIX: Removed duplicate close button rule (was at original lines 592-601,
 *      duplicating 548-562). Combined titleClose/close selector covers both.
 * FIX: Removed hardcoded titleBar background (#1A1A1A at original line 514).
 *      The tokenized version lives in 08_Containers.h.
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString windowChrome() {
  return QStringLiteral(R"QSS(

/* ================================================================
   TITLE BAR — custom frameless window chrome
   ================================================================ */

/* --- Title Bar: Minimize (warm gold hover) --- */
QPushButton[role="titleMinimize"] {
    background: {{btn.titleMinimize.bg}};
    border: 1px solid {{btn.titleMinimize.border}};
    border-radius: 4px;
}
QPushButton[role="titleMinimize"]:hover {
    background: {{btn.titleMinimize.hoverBg}};
    border-color: {{btn.titleMinimize.hoverBorder}};
}
QPushButton[role="titleMinimize"]:pressed {
    background: {{btn.titleMinimize.pressedBg}};
}

/* --- Title Bar: Tray (blue hover) --- */
QPushButton[role="titleTray"] {
    background: {{btn.titleTray.bg}};
    border: 1px solid {{btn.titleTray.border}};
    border-radius: 4px;
}
QPushButton[role="titleTray"]:hover {
    background: {{btn.titleTray.hoverBg}};
    border-color: {{btn.titleTray.hoverBorder}};
}
QPushButton[role="titleTray"]:pressed {
    background: {{btn.titleTray.pressedBg}};
}

/* --- Title Bar: Close (red hover) --- */
QPushButton[role="titleClose"],
QPushButton[role="close"] {
    background: {{btn.close.bg}};
    border: 1px solid {{btn.close.border}};
    border-radius: 4px;
}
QPushButton[role="titleClose"]:hover,
QPushButton[role="close"]:hover {
    background: {{btn.close.hoverBg}};
    border-color: {{btn.close.hoverBorder}};
}
QPushButton[role="titleClose"]:pressed,
QPushButton[role="close"]:pressed {
    background: {{btn.close.pressedBg}};
}

/* ================================================================
   NAV SIDEBAR BUTTONS
   ================================================================ */

/* --- Nav Active (selected sidebar button) --- */
QPushButton[role="navActive"] {
    background-color: {{btn.navActive.bg}};
    border: none;
    border-radius: 8px;
    padding: 12px 15px;
    text-align: left;
    font-size: {{layout.fontSize.normal}}px;
    color: {{btn.navActive.text}};
    font-weight: bold;
}

/* --- Nav Inactive (unselected sidebar button) --- */
QPushButton[role="navInactive"] {
    background-color: {{btn.navInactive.bg}};
    border: none;
    border-radius: 8px;
    padding: 12px 15px;
    text-align: left;
    font-size: {{layout.fontSize.normal}}px;
    color: {{btn.navInactive.text}};
    font-weight: normal;
}
QPushButton[role="navInactive"]:hover {
    background-color: {{btn.navInactive.hoverBg}};
    color: {{btn.navInactive.hoverText}};
}

)QSS");
}

} // namespace QssTemplate
