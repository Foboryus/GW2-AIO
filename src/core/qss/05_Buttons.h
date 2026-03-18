/**
 * @file 05_Buttons.h
 * @brief QSS Section 5: All role-based button styles
 *
 * FIX: action button font-size changed from hardcoded 13px
 *      to themable {{layout.fontSize.normal}}px
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString buttons() {
  return QStringLiteral(R"QSS(

/* ================================================================
   BUTTONS — role-based property selectors
   Widgets set: btn->setProperty("role", "primary");
   ================================================================ */

/* --- Generic QPushButton base (unstyled buttons) --- */
QPushButton {
    background: {{btn.neutral.bg}};
    color: {{btn.neutral.text}};
    border: 1px solid {{btn.neutral.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton:hover {
    background: {{btn.neutral.hoverBg}};
    border-color: {{btn.neutral.hoverBorder}};
}
QPushButton:pressed {
    background: {{btn.neutral.pressedBg}};
}

/* --- Primary (Launch, main CTAs) --- */
QPushButton[role="primary"] {
    background: {{btn.primary.bg}};
    color: {{btn.primary.text}};
    font-weight: bold;
    border: 2px solid {{btn.primary.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="primary"]:hover {
    background: {{btn.primary.hoverBg}};
    border-color: {{btn.primary.hoverBorder}};
    color: {{btn.primary.hoverText}};
}
QPushButton[role="primary"]:pressed {
    background: {{btn.primary.pressedBg}};
}

/* --- Confirm (OK, Save, Yes) --- */
QPushButton[role="confirm"] {
    background: {{btn.confirm.bg}};
    color: {{btn.confirm.text}};
    font-weight: bold;
    border: 1px solid {{btn.confirm.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="confirm"]:hover {
    background: {{btn.confirm.hoverBg}};
    border-color: {{btn.confirm.hoverBorder}};
}
QPushButton[role="confirm"]:pressed {
    background: {{btn.confirm.pressedBg}};
}

/* --- Cancel (Cancel, Delete, Exit) --- */
QPushButton[role="cancel"] {
    background: {{btn.cancel.bg}};
    color: {{btn.cancel.text}};
    font-weight: bold;
    border: 1px solid {{btn.cancel.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="cancel"]:hover {
    background: {{btn.cancel.hoverBg}};
    border-color: {{btn.cancel.hoverBorder}};
    color: {{btn.cancel.hoverText}};
}
QPushButton[role="cancel"]:pressed {
    background: {{btn.cancel.pressedBg}};
}

/* --- Neutral (Edit, Browse, misc) --- */
QPushButton[role="neutral"] {
    background: {{btn.neutral.bg}};
    color: {{btn.neutral.text}};
    font-weight: bold;
    border: 1px solid {{btn.neutral.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="neutral"]:hover {
    background: {{btn.neutral.hoverBg}};
    border-color: {{btn.neutral.hoverBorder}};
}
QPushButton[role="neutral"]:pressed {
    background: {{btn.neutral.pressedBg}};
}

/* --- Action (urgent, like Update) --- */
QPushButton[role="action"] {
    background: {{btn.action.bg}};
    color: {{btn.action.text}};
    font-weight: bold;
    font-size: {{layout.fontSize.normal}}px;
    border: 1px solid {{btn.action.border}};
    border-radius: 8px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="action"]:hover {
    background: {{btn.action.hoverBg}};
}
QPushButton[role="action"]:pressed {
    background: {{btn.action.pressedBg}};
}

/* --- Apply Dirty (unsaved changes) --- */
QPushButton[role="applyDirty"] {
    background: {{btn.applyDirty.bg}};
    color: {{btn.applyDirty.text}};
    font-weight: bold;
    border: 2px solid {{btn.applyDirty.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="applyDirty"]:hover {
    background: {{btn.applyDirty.hoverBg}};
    border-color: {{btn.applyDirty.hoverBorder}};
    color: {{btn.applyDirty.hoverText}};
}
QPushButton[role="applyDirty"]:pressed {
    background: {{btn.applyDirty.pressedBg}};
}

/* --- Apply Clean (all saved) --- */
QPushButton[role="applyClean"] {
    background: {{btn.applyClean.bg}};
    color: {{btn.applyClean.text}};
    font-weight: bold;
    border: 1px solid {{btn.applyClean.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: {{layout.buttonPadding}};
}
QPushButton[role="applyClean"]:hover {
    background: {{btn.applyClean.hoverBg}};
    color: {{btn.applyClean.hoverText}};
    border-color: {{btn.applyClean.hoverBorder}};
}
QPushButton[role="applyClean"]:pressed {
    background: {{btn.applyClean.pressedBg}};
}

)QSS");
}

} // namespace QssTemplate
