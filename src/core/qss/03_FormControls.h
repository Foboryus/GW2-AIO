/**
 * @file 03_FormControls.h
 * @brief QSS Section 3: GroupBox, input fields, combobox
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString formControls() {
  return QStringLiteral(R"QSS(

/* ================================================================
   GROUP BOX
   ================================================================ */

QGroupBox {
    font-weight: bold;
    border: 1px solid {{widget.groupbox.border}};
    border-radius: 4px;
    margin-top: 8px;
    padding-top: 8px;
}

QGroupBox::title {
    color: {{widget.groupbox.title}};
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 8px;
    padding: 0 4px;
}

/* ================================================================
   INPUT FIELDS
   ================================================================ */

QLineEdit, QSpinBox {
    background: {{widget.input.bg}};
    color: {{text.primary}};
    border: 1px solid {{widget.input.border}};
    border-radius: {{layout.borderRadius}}px;
    padding: 4px 8px;
    font-size: {{layout.fontSize.normal}}px;
}

QLineEdit:focus, QSpinBox:focus {
    border-color: {{widget.input.focusBorder}};
}

/* ================================================================
   COMBOBOX
   ================================================================ */

QComboBox {
    background: {{widget.combobox.bg}};
    color: {{text.primary}};
    border: 1px solid {{widget.combobox.border}};
    border-radius: 4px;
    padding: 4px 8px 4px 12px;
    min-height: 22px;
}

QComboBox:hover {
    border-color: {{widget.input.border}};
}

QComboBox:focus {
    border-color: {{widget.input.focusBorder}};
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border-left: 1px solid {{widget.combobox.border}};
    border-radius: 0 4px 4px 0;
    background: {{widget.combobox.dropdownBg}};
}

QComboBox::drop-down:hover {
    background: {{widget.combobox.dropdownHover}};
}

QComboBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 12px;
    height: 12px;
}

QComboBox QAbstractItemView {
    background: {{widget.combobox.bg}};
    color: {{text.primary}};
    padding-left: 8px;
    selection-background-color: {{widget.combobox.selectionBg}};
    border: 1px solid {{widget.combobox.border}};
}

)QSS");
}

} // namespace QssTemplate
