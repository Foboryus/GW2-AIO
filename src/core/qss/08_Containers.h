/**
 * @file 08_Containers.h
 * @brief QSS Section 8: Container roles, dialog roles, titleBar widget
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString containers() {
  return QStringLiteral(R"QSS(

/* ================================================================
   CONTAINERS — role-based
   ================================================================ */

QWidget[role="popupBackground"] {
    background: {{widget.popup.bg}};
    border: 3px solid {{widget.popup.border}};
    border-radius: {{layout.popupBorderRadius}}px;
}

QWidget[role="windowBackground"] {
    background: {{window.surface}};
    border-radius: {{layout.popupBorderRadius}}px;
}

QWidget[role="container"] {
    background-color: {{container.bg}};
    border: none;
    border-radius: 8px;
}

QWidget[role="overlay"] {
    background-color: {{overlay.bg}};
    border: none;
    border-radius: 8px;
    padding: 10px;
}

QWidget[role="titleBar"] {
    background-color: {{titlebar.bg}};
    border: none;
    border-radius: 7px;
}

QWidget[role="titleBarLabel"] {
    color: {{text.accent}};
    font-size: {{layout.fontSize.title}}px;
    font-weight: bold;
    background: transparent;
    border: none;
}

QWidget[role="saveIndicator"] {
    background: transparent;
    border: none;
    padding: 0;
}

QWidget[role="transparent"] {
    background: transparent;
    border: none;
}

/* ================================================================
   DIALOG (translucent background for rounded corners)
   ================================================================ */

QDialog[role="styledDialog"] {
    background: transparent;
}

QDialog[role="dialog"] {
    background-color: {{window.surface}};
    border: 3px solid {{widget.popup.border}};
    border-radius: 10px;
}

/* ================================================================
   MARKER PACK CARD (accordion-style, Profile Editor Markers tab)
   ================================================================ */

QWidget[role="markerPackCard"] {
    background-color: {{widget.packCard.bg}};
    border: 1px solid {{widget.packCard.border}};
    border-radius: {{layout.borderRadius}}px;
    margin-bottom: 2px;
}
QWidget[role="markerPackCard"]:hover {
    border-color: {{widget.packCard.hoverBorder}};
}

QLabel[role="cardTitle"] {
    color: {{text.primary}};
    font-weight: bold;
    font-size: {{layout.fontSize.normal}}px;
    background: transparent;
    border: none;
}

/* ================================================================
   SUB-TAB BUTTONS (Packs | Settings within Markers tab)
   ================================================================ */

QPushButton[role="subTabActive"] {
    background: transparent;
    border: none;
    border-bottom: 2px solid {{text.accent}};
    color: {{text.accent}};
    font-weight: bold;
    padding: 4px 16px;
}
QPushButton[role="subTabActive"]:hover {
    background: {{text.accentSubtle}};
}

QPushButton[role="subTabInactive"] {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    color: {{text.hint}};
    font-weight: normal;
    padding: 4px 16px;
}
QPushButton[role="subTabInactive"]:hover {
    color: {{text.secondary}};
    border-bottom: 2px solid {{widget.tab.border}};
}

)QSS");
}

} // namespace QssTemplate
