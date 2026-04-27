/**
 * @file 07_Labels.h
 * @brief QSS Section 7: All role-based QLabel styles + badge
 *
 * FIX: goldTitle and errorTitle font-size changed from hardcoded 16px
 *      to themable {{layout.fontSize.title}}px
 */
#pragma once
#include <QString>

namespace QssTemplate {

inline QString labels() {
  return QStringLiteral(R"QSS(

/* ================================================================
   LABELS — role-based property selectors
   Widgets set: label->setProperty("role", "pageTitle");
   ================================================================ */

QLabel[role="pageTitle"] {
    font-size: {{layout.fontSize.pageTitle}}px;
    font-weight: bold;
    color: {{text.accent}};
    background: transparent;
    border: none;
}

QLabel[role="label"] {
    color: {{text.primary}};
    background: transparent;
    border: none;
}

QLabel[role="secondary"] {
    color: {{text.secondary}};
    background: transparent;
    border: none;
}

QLabel[role="hint"] {
    color: {{text.hint}};
    font-size: {{layout.fontSize.hint}}px;
    font-style: italic;
    background: transparent;
    border: none;
}

QLabel[role="status"] {
    color: {{text.secondary}};
    padding: 8px;
    background: transparent;
    border: none;
}

QLabel[role="goldTitle"] {
    color: {{text.accent}};
    font-weight: bold;
    font-size: {{layout.fontSize.title}}px;
    border: none;
    background: transparent;
}

QLabel[role="errorTitle"] {
    color: {{text.error}};
    font-weight: bold;
    font-size: {{layout.fontSize.title}}px;
    border: none;
    background: transparent;
}

QLabel[role="successLabel"] {
    color: {{text.success}};
    border: none;
    font-weight: bold;
    background: transparent;
}

QLabel[role="popupLabel"] {
    color: {{text.primary}};
    border: none;
    background: transparent;
}

QLabel[role="warningColor"] {
    color: {{text.warning}};
    background: transparent;
    border: none;
}

QLabel[role="successColor"] {
    color: {{text.success}};
    background: transparent;
    border: none;
}

QLabel[role="goldColor"] {
    color: {{text.accent}};
    background: transparent;
    border: none;
}

QLabel[role="errorColor"] {
    color: {{text.error}};
    background: transparent;
    border: none;
}

/* ================================================================
   BADGE
   ================================================================ */

QLabel[role="badge"] {
    background-color: {{widget.badge.bg}};
    color: {{widget.badge.text}};
    font-size: {{layout.fontSize.badge}}px;
    font-weight: bold;
    padding: 2px 6px;
    border-radius: 8px;
}

/* ================================================================
   INFO BANNER (markers tab: missing/failed pack notification)
   ================================================================ */

QLabel[role="infoBanner"] {
    background-color: {{widget.infoBanner.bg}};
    color: {{widget.infoBanner.text}};
    border: 1px solid {{widget.infoBanner.border}};
    border-radius: 4px;
    padding: 8px;
    font-size: 12px;
}

/* ================================================================
   PROFILE BADGE PILL (API badge on launcher cards)
   ================================================================ */

QLabel[role="profileBadgePill"] {
    background-color: {{profileBadge.pillBg}};
    color: {{profileBadge.pillText}};
    border: 1px solid {{profileBadge.pillBorder}};
    border-radius: 10px;
    padding: 1px 6px;
    font-size: {{layout.fontSize.badge}}px;
    font-weight: bold;
}

)QSS");
}

} // namespace QssTemplate
