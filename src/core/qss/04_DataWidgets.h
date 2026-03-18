/**
 * @file 04_DataWidgets.h
 * @brief QSS Section 4: List, tab, slider, scrollbar widgets
 *
 * FIX: Added color tokens to QTabBar::tab and QTabBar::tab:selected
 *      (missing in original — tabs had no text color control)
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString dataWidgets() {
  return QStringLiteral(R"QSS(

/* ================================================================
   LIST WIDGET
   ================================================================ */

QListWidget {
    background: {{widget.list.bg}};
    border: 1px solid {{widget.list.border}};
    border-radius: 4px;
    outline: none;
}

QListWidget::item {
    padding: 4px 0px;
    border: none;
    border-bottom: 1px solid {{widget.list.border}};
}

QListWidget::item:selected {
    background: {{widget.list.selectedBg}};
    color: {{widget.list.selectedText}};
}

QListWidget::item:hover {
    background: rgba(255, 255, 255, 0.03);
}

QListWidget:focus {
    outline: none;
    border: 1px solid {{widget.input.focusBorder}};
}

/* ================================================================
   TAB WIDGET
   ================================================================ */

QTabWidget::pane {
    border: 1px solid {{widget.tab.border}};
}

QTabBar::tab {
    background: {{widget.tab.bg}};
    color: {{widget.tab.text}};
    padding: 8px 16px;
    border: 1px solid {{widget.tab.border}};
}

QTabBar::tab:selected {
    background: {{widget.tab.selectedBg}};
    color: {{widget.tab.selectedText}};
    border-bottom: 2px solid {{widget.tab.selectedBorder}};
}

/* ================================================================
   SLIDER
   ================================================================ */

QSlider::groove:horizontal {
    height: 6px;
    background: {{widget.slider.groove}};
    border-radius: 3px;
}

QSlider::handle:horizontal {
    width: 16px;
    height: 16px;
    background: {{widget.slider.handle}};
    border-radius: 8px;
    margin: -5px 0;
}

/* ================================================================
   SCROLLBAR
   ================================================================ */

QScrollBar:vertical {
    width: 12px;
    background: {{widget.scrollbar.track}};
}

QScrollBar::handle:vertical {
    background: {{widget.scrollbar.handle}};
    border-radius: 6px;
    min-height: 20px;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

/* ================================================================
   TABLE WIDGET
   ================================================================ */

QTableWidget {
    background: {{widget.list.bg}};
    border: 1px solid {{widget.list.border}};
    border-radius: 4px;
    color: {{text.primary}};
    gridline-color: {{widget.list.border}};
}

QTableWidget::item {
    padding: 4px;
}

QTableWidget::item:selected {
    background: {{widget.list.selectedBg}};
    color: {{widget.list.selectedText}};
}

QHeaderView::section {
    background: {{container.bg}};
    color: {{text.secondary}};
    padding: 6px;
    border: none;
    border-bottom: 1px solid {{widget.list.border}};
}


/* ================================================================
   PROGRESS BAR
   ================================================================ */

QProgressBar {
    background: {{container.bg}};
    border: 1px solid {{widget.list.border}};
    border-radius: 3px;
    max-height: 8px;
}

QProgressBar::chunk {
    background: {{text.success}};
    border-radius: 3px;
}

)QSS");
}

} // namespace QssTemplate
