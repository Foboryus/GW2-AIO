/**
 * @file ThemeManager.cpp
 * @brief Fashion Wars theme engine — QSS template compilation, icon recoloring
 *
 * Architecture: Approach B+ (Template + Property Hybrid)
 *
 * The master QSS template uses {{token}} placeholders that get replaced with
 * theme color values on each theme switch. The compiled QSS is then applied
 * globally via qApp->setStyleSheet(), which automatically re-evaluates all
 * property-based selectors across all widgets.
 *
 * Widgets use setProperty("role", "primary") etc. which are matched by
 * QPushButton[role="primary"] { ... } rules in the global QSS.
 */

#include "ThemeManager.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QTextStream>

// Modular QSS template sections (concatenated in cascade order)
#include "qss/01_Base.h"
#include "qss/02_Navigation.h"
#include "qss/03_FormControls.h"
#include "qss/04_DataWidgets.h"
#include "qss/05_Buttons.h"
#include "qss/06_WindowChrome.h"
#include "qss/07_Labels.h"
#include "qss/08_Containers.h"
#include "qss/09_Misc.h"

// ============================================================================
// Singleton
// ============================================================================

ThemeManager &ThemeManager::instance() {
  static ThemeManager instance;
  return instance;
}

// ============================================================================
// Theme switching
// ============================================================================

void ThemeManager::setBuiltinTheme(BuiltinTheme theme) {
  m_currentBuiltin = theme;
  switch (theme) {
  case BuiltinTheme::ClassicGold:
    m_activeTheme = ThemeData::classicGold();
    break;
  case BuiltinTheme::Light:
    m_activeTheme = ThemeData::light();
    break;
  case BuiltinTheme::NavyBlue:
    m_activeTheme = ThemeData::navyBlue();
    break;
  }
  compileAndApply();
}

void ThemeManager::setCustomTheme(const ThemeData &data) {
  m_activeTheme = data;
  compileAndApply();
}

// ============================================================================
// Template compilation — replaces {{tokens}} with theme values
// ============================================================================

void ThemeManager::compileAndApply() {
  QString qss = masterTemplate();

  // --- Colors ---
  const auto &c = m_activeTheme.colors;
  qss.replace("{{window.bg}}", c.windowBg);
  qss.replace("{{window.surface}}", c.windowSurface);
  qss.replace("{{titlebar.bg}}", c.titleBarBg);
  qss.replace("{{container.bg}}", c.containerBg);
  qss.replace("{{overlay.bg}}", c.overlayBg);
  qss.replace("{{text.primary}}", c.textPrimary);
  qss.replace("{{text.secondary}}", c.textSecondary);
  qss.replace("{{text.hint}}", c.textHint);
  qss.replace("{{text.accent}}", c.textAccent);
  qss.replace("{{text.accentSubtle}}", c.textAccentSubtle);
  qss.replace("{{text.success}}", c.success);
  qss.replace("{{text.warning}}", c.warning);
  qss.replace("{{text.error}}", c.error);

  // --- Button tokens (helper lambda to avoid repetition) ---
  auto replaceBtn = [&qss](const QString &prefix, const ButtonTokens &bt) {
    qss.replace("{{" + prefix + ".bg}}", bt.bg);
    qss.replace("{{" + prefix + ".text}}", bt.text);
    qss.replace("{{" + prefix + ".border}}", bt.border);
    qss.replace("{{" + prefix + ".hoverBg}}", bt.hoverBg);
    qss.replace("{{" + prefix + ".hoverBorder}}",
                bt.hoverBorder.isEmpty() ? bt.border : bt.hoverBorder);
    qss.replace("{{" + prefix + ".hoverText}}",
                bt.hoverText.isEmpty() ? bt.text : bt.hoverText);
    qss.replace("{{" + prefix + ".pressedBg}}",
                bt.pressedBg.isEmpty() ? bt.bg : bt.pressedBg);
  };

  const auto &b = m_activeTheme.buttons;
  replaceBtn("btn.primary", b.primary);
  replaceBtn("btn.confirm", b.confirm);
  replaceBtn("btn.cancel", b.cancel);
  replaceBtn("btn.neutral", b.neutral);
  replaceBtn("btn.action", b.action);
  replaceBtn("btn.applyDirty", b.applyDirty);
  replaceBtn("btn.applyClean", b.applyClean);
  replaceBtn("btn.navActive", b.navActive);
  replaceBtn("btn.navInactive", b.navInactive);
  replaceBtn("btn.close", b.close);
  replaceBtn("btn.titleMinimize", b.titleMinimize);
  replaceBtn("btn.titleTray", b.titleTray);

  // --- Widget tokens ---
  const auto &w = m_activeTheme.widgets;
  qss.replace("{{widget.groupbox.border}}", w.groupboxBorder);
  qss.replace("{{widget.groupbox.title}}", w.groupboxTitle);
  qss.replace("{{widget.input.bg}}", w.inputBg);
  qss.replace("{{widget.input.border}}", w.inputBorder);
  qss.replace("{{widget.input.focusBorder}}", w.inputFocusBorder);
  qss.replace("{{widget.combobox.bg}}", w.comboboxBg);
  qss.replace("{{widget.combobox.border}}", w.comboboxBorder);
  qss.replace("{{widget.combobox.dropdownBg}}", w.comboboxDropdownBg);
  qss.replace("{{widget.combobox.dropdownHover}}", w.comboboxDropdownHover);
  qss.replace("{{widget.combobox.selectionBg}}", w.comboboxSelectionBg);
  qss.replace("{{widget.popup.bg}}", w.popupBg);
  qss.replace("{{widget.popup.border}}", w.popupBorder);
  qss.replace("{{widget.scrollbar.handle}}", w.scrollbarHandle);
  qss.replace("{{widget.scrollbar.track}}", w.scrollbarTrack);
  qss.replace("{{widget.list.bg}}", w.listBg);
  qss.replace("{{widget.list.border}}", w.listBorder);
  qss.replace("{{widget.list.selectedBg}}", w.listSelectedBg);
  qss.replace("{{widget.list.selectedText}}", w.listSelectedText);
  qss.replace("{{widget.tab.bg}}", w.tabBg);
  qss.replace("{{widget.tab.border}}", w.tabBorder);
  qss.replace("{{widget.tab.selectedBg}}", w.tabSelectedBg);
  qss.replace("{{widget.tab.selectedBorder}}", w.tabSelectedBorder);
  qss.replace("{{widget.tab.text}}", w.tabText);
  qss.replace("{{widget.tab.selectedText}}", w.tabSelectedText);
  qss.replace("{{widget.slider.groove}}", w.sliderGroove);
  qss.replace("{{widget.slider.handle}}", w.sliderHandle);
  qss.replace("{{widget.badge.bg}}", w.badgeBg);
  qss.replace("{{widget.badge.text}}", w.badgeText);
  qss.replace("{{widget.infoBanner.bg}}", w.infoBannerBg);
  qss.replace("{{widget.infoBanner.border}}", w.infoBannerBorder);
  qss.replace("{{widget.infoBanner.text}}", w.infoBannerText);
  qss.replace("{{widget.pageHeader.bg}}", w.pageHeaderBg);
  qss.replace("{{widget.pageHeader.border}}", w.pageHeaderBorder);
  qss.replace("{{widget.pageHeader.text}}",
              w.pageHeaderText.isEmpty() ? c.textAccent : w.pageHeaderText);
  qss.replace("{{widget.packCard.bg}}", w.packCardBg);
  qss.replace("{{widget.packCard.border}}", w.packCardBorder);
  qss.replace("{{widget.packCard.hoverBorder}}", w.packCardHoverBorder);

  // --- Layout tokens ---
  const auto &l = m_activeTheme.layout;
  qss.replace("{{layout.borderRadius}}", QString::number(l.borderRadius));
  qss.replace("{{layout.popupBorderRadius}}",
              QString::number(l.popupBorderRadius));
  qss.replace("{{layout.buttonPadding}}", l.buttonPadding);
  qss.replace("{{layout.fontSize.normal}}", QString::number(l.fontSizeNormal));
  qss.replace("{{layout.fontSize.title}}", QString::number(l.fontSizeTitle));
  qss.replace("{{layout.fontSize.pageTitle}}",
              QString::number(l.fontSizePageTitle));
  qss.replace("{{layout.fontSize.hint}}", QString::number(l.fontSizeHint));
  qss.replace("{{layout.fontSize.badge}}", QString::number(l.fontSizeBadge));

  // Cache and apply
  m_compiledQss = qss;
  qApp->setStyleSheet(m_compiledQss);

  // Rebuild icon cache with new theme colors
  rebuildIconCache();

  // Re-apply themed icons to all live widgets
  refreshThemedIcons();

  emit themeChanged();
}

// ============================================================================
// Master QSS template — concatenates modular sections
// ============================================================================

QString ThemeManager::masterTemplate() {
  // Concatenate modular QSS sections. Order matters for cascade:
  // base -> navigation -> form controls -> data widgets -> buttons
  // -> window chrome -> labels -> containers -> misc (tooltip last).
  return QssTemplate::base() + QssTemplate::navigation() +
         QssTemplate::formControls() + QssTemplate::dataWidgets() +
         QssTemplate::buttons() + QssTemplate::windowChrome() +
         QssTemplate::labels() + QssTemplate::containers() +
         QssTemplate::misc();
}

// ============================================================================
// SVG icon recoloring
// ============================================================================

QString ThemeManager::recolorSvg(const QString &svgContent,
                                 const QString &oldColor,
                                 const QString &newColor) {
  if (oldColor == newColor) {
    return svgContent;
  }

  QString result = svgContent;

  // Replace stroke="oldColor" and fill="oldColor" (case-insensitive)
  // Handle both quoted forms: stroke="#C09C57" and stroke='#C09C57'
  QRegularExpression strokeRx(QString(R"(stroke\s*=\s*["']%1["'])")
                                  .arg(QRegularExpression::escape(oldColor)),
                              QRegularExpression::CaseInsensitiveOption);
  result.replace(strokeRx, QString(R"(stroke="%1")").arg(newColor));

  QRegularExpression fillRx(QString(R"(fill\s*=\s*["']%1["'])")
                                .arg(QRegularExpression::escape(oldColor)),
                            QRegularExpression::CaseInsensitiveOption);
  result.replace(fillRx, QString(R"(fill="%1")").arg(newColor));

  return result;
}

void ThemeManager::rebuildIconCache() {
  m_iconCache.clear();

  const QString newColor = m_activeTheme.colors.iconColor;

  // If icon color matches default, no recoloring needed — use originals
  if (newColor.compare(kDefaultIconColor, Qt::CaseInsensitive) == 0) {
    return;
  }

  // Scan all SVG resources and recolor them
  QDir iconsDir(":/icons");
  const auto entries = iconsDir.entryList({"*.svg"}, QDir::Files);
  for (const QString &filename : entries) {
    QFile file(":/icons/" + filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      continue;
    }
    QString svgContent = QTextStream(&file).readAll();
    file.close();

    QString recolored = recolorSvg(svgContent, kDefaultIconColor, newColor);
    if (recolored != svgContent) {
      // Create QIcon from recolored SVG data
      QSvgRenderer renderer(recolored.toUtf8());
      if (renderer.isValid()) {
        // Render at standard sizes
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        renderer.render(&painter);
        painter.end();

        QString baseName = filename;
        baseName.chop(4); // Remove ".svg"
        m_iconCache.insert(baseName, QIcon(pix));
      }
    }
  }
}

QIcon ThemeManager::icon(const QString &name) const {
  // Check cache first (recolored icons)
  auto it = m_iconCache.find(name);
  if (it != m_iconCache.end()) {
    return it.value();
  }
  // Fall back to default resource icon
  return QIcon(":/icons/" + name + ".svg");
}

void ThemeManager::refreshThemedIcons() {
  const auto allWidgets = QApplication::allWidgets();
  for (QWidget *w : allWidgets) {
    QVariant nameVar = w->property("themedIconName");
    if (!nameVar.isValid() || nameVar.toString().isEmpty()) {
      continue;
    }
    const QString iconName = nameVar.toString();
    const QIcon themed = icon(iconName);

    if (auto *btn = qobject_cast<QPushButton *>(w)) {
      btn->setIcon(themed);
    } else if (auto *lbl = qobject_cast<QLabel *>(w)) {
      int sz = w->property("themedIconSize").toInt();
      if (sz > 0) {
        lbl->setPixmap(themed.pixmap(sz, sz));
      }
    }
  }
}

// ============================================================================
// Utility
// ============================================================================

QString ThemeManager::themeName(BuiltinTheme theme) {
  switch (theme) {
  case BuiltinTheme::ClassicGold:
    return QStringLiteral("Classic Gold");
  case BuiltinTheme::Light:
    return QStringLiteral("Light");
  case BuiltinTheme::NavyBlue:
    return QStringLiteral("Navy Blue");
  }
  return QStringLiteral("Unknown");
}

QList<ThemeManager::BuiltinTheme> ThemeManager::builtinThemes() {
  return {BuiltinTheme::ClassicGold, BuiltinTheme::Light,
          BuiltinTheme::NavyBlue};
}
