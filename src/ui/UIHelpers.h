#pragma once
// UIHelpers.h - Centralized UI styling and positioning for popup dialogs

#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

#include "core/ThemeManager.h"

namespace UIHelpers {

// ============================================================================
// LEGACY xxxStyle() FUNCTIONS — REMOVED
// All replaced by role-based applyXxx() functions below that go through
// ThemeManager's global QSS. See ThemeManager::compileAndApply() for the
// QSS template that maps roles to theme tokens.
// ============================================================================

// ============================================================================
// POSITIONING
// ============================================================================

/**
 * @brief Center dialog on primary screen (upper third)
 */
inline void centerDialog(QDialog *dialog) {
  dialog->adjustSize();
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    QRect geom = screen->availableGeometry();
    int x = geom.x() + (geom.width() - dialog->width()) / 2;
    int y = geom.y() + geom.height() / 6; // Upper third
    dialog->move(x, y);
  }
}

// ============================================================================
// HELPER WIDGETS
// ============================================================================

// Forward declarations of role-setter functions (defined later in this header)
// needed by helper widget factories below.
inline void applyRole(QWidget *w, const QString &role);
inline void applyPrimaryStyle(QPushButton *btn);
inline void applyCancelStyle(QPushButton *btn);
inline void applyCloseStyle(QPushButton *btn);
inline void applyLabelRole(QLabel *lbl);
inline void applyLabelRole(QLabel *lbl, int fontSize);
inline void applyGoldTitleRole(QLabel *lbl);
inline void applyErrorTitleRole(QLabel *lbl);
inline void applySuccessLabelRole(QLabel *lbl);
inline void applyPopupLabelRole(QLabel *lbl);
inline void applyPopupBackgroundRole(QWidget *w);
inline void applyContainerRole(QWidget *w);
inline void applyStyledDialogRole(QDialog *d);
inline void applySaveIndicatorRole(QWidget *w);

/**
 * @brief Get a themed icon by base name (no extension, no path prefix).
 * Returns the icon recolored to match the active theme's iconColor.
 * Falls back to the original resource SVG if not in the cache.
 * @param name Icon base name, e.g. "play-circle", "settings"
 */
inline QIcon themedIcon(const QString &name) {
  return ThemeManager::instance().icon(name);
}

/**
 * @brief Set a themed icon on a QPushButton and store the icon name
 * as a property so ThemeManager::refreshThemedIcons() can update it
 * when the theme changes.
 */
inline void setThemedIcon(QPushButton *btn, const QString &name) {
  btn->setIcon(ThemeManager::instance().icon(name));
  btn->setProperty("themedIconName", name);
}

/**
 * @brief Set a themed pixmap on a QLabel and store the icon name/size
 * as properties so ThemeManager::refreshThemedIcons() can update it
 * when the theme changes.
 */
inline void setThemedPixmap(QLabel *lbl, const QString &name, int size) {
  lbl->setPixmap(ThemeManager::instance().icon(name).pixmap(size, size));
  lbl->setProperty("themedIconName", name);
  lbl->setProperty("themedIconSize", size);
}

/**
 * @brief Create a styled message container (dark box with rounded corners)
 * @return Container widget with VBoxLayout already set
 */
inline QWidget *createMessageContainer(QWidget *parent) {
  auto *container = new QWidget(parent);
  container->setObjectName("messageContainer"); // For style targeting
  applyContainerRole(container);
  auto *layout = new QVBoxLayout(container);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(15);
  return container;
}

/**
 * @brief Create a styled dialog with standard flags and styling
 * Uses translucent background for proper rounded corners on Windows
 */
inline QDialog *createStyledDialog(QWidget *parent, int minWidth = 400) {
  auto *dialog = new QDialog(parent);
  dialog->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                         Qt::FramelessWindowHint);
  dialog->setAttribute(
      Qt::WA_TranslucentBackground); // Required for rounded corners
  dialog->setMinimumWidth(minWidth);
  applyStyledDialogRole(dialog); // Role-based: no cascade to children
  return dialog;
}

/**
 * @brief Create a standard label with styling
 */
inline QLabel *createLabel(QWidget *parent, const QString &text,
                           int fontSize = -1) {
  auto *label = new QLabel(text, parent);
  if (fontSize <= 0)
    fontSize = ThemeManager::instance().activeTheme().layout.fontSizeNormal;
  applyLabelRole(label, fontSize);
  label->setWordWrap(true);
  return label;
}

/**
 * @brief Create a custom title bar for frameless dialogs
 * @param parent The parent widget
 * @param title The title text to display
 * @param iconName Icon base name (e.g. "profile-game"), no path/extension
 * @param closeCallback Function to call when close button is clicked
 * @param saveIndicatorOut Optional out-parameter: receives a QLabel* placed
 *        next to the close button, for showing save-state icons
 * @return Title bar widget with icon, title, and close button
 */
inline QWidget *createTitleBar(QWidget *parent, const QString &title,
                               const QString &iconName = "profile-game",
                               std::function<void()> closeCallback = nullptr,
                               QLabel **saveIndicatorOut = nullptr) {
  auto *titleBar = new QWidget(parent);
  applyRole(titleBar, "titleBar");
  auto *titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(12, 10, 10, 10);

  // Logo/icon (larger)
  auto *iconLabel = new QLabel(titleBar);
  // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
  iconLabel->setStyleSheet("background: transparent; border: none;");
  setThemedPixmap(iconLabel, iconName, 24);
  titleLayout->addWidget(iconLabel);

  // Title (larger font) — themed
  auto *titleLabel = new QLabel(title, titleBar);
  const auto &tc = ThemeManager::instance().activeTheme().colors;
  // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
  titleLabel->setStyleSheet(
      QString("color: %1; font-size: %2px; font-weight: bold; margin-left: "
              "10px; background: transparent; border: none;")
          .arg(tc.textAccent)
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeTitle));
  titleLayout->addWidget(titleLabel);

  titleLayout->addStretch();

  // Save indicator icon (optional, placed before close button)
  if (saveIndicatorOut) {
    auto *indicator = new QLabel(titleBar);
    indicator->setFixedSize(22, 22);
    applySaveIndicatorRole(indicator);
    // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
    indicator->setStyleSheet("background: transparent; border: none;");
    setThemedPixmap(indicator, "check-circle", 18);
    indicator->setToolTip("All changes saved");
    titleLayout->addWidget(indicator);
    titleLayout->addSpacing(6);
    *saveIndicatorOut = indicator;
  }

  // Close button with X icon — themed widget-level stylesheet
  auto *closeBtn = new QPushButton(titleBar);
  setThemedIcon(closeBtn, "x");
  closeBtn->setIconSize(QSize(14, 14));
  closeBtn->setFixedSize(28, 28);
  const auto &ct = ThemeManager::instance().activeTheme().buttons.close;
  // REVIEW BEFORE BETA: inline setStyleSheet for close button (uses ThemeManager values)
  closeBtn->setStyleSheet(
      QString("QPushButton { background: %1; border: 1px solid %2; "
              "border-radius: 4px; }"
              "QPushButton:hover { background: %3; border-color: %4; }"
              "QPushButton:pressed { background: %5; }")
          .arg(ct.bg, ct.border, ct.hoverBg,
               ct.hoverBorder.isEmpty() ? ct.border : ct.hoverBorder,
               ct.pressedBg.isEmpty() ? ct.bg : ct.pressedBg));
  closeBtn->setToolTip("Close");

  if (closeCallback) {
    QObject::connect(closeBtn, &QPushButton::clicked, closeCallback);
  }
  titleLayout->addWidget(closeBtn);

  return titleBar;
}
// ============================================================================
// MESSAGE DIALOGS — one-liner replacements for inline popup boilerplate
// ============================================================================

/**
 * @brief Show a simple info/warning dialog with OK button (gold title)
 */
inline void showInfoDialog(QWidget *parent, const QString &message,
                           const QString &title = QString(),
                           int minWidth = 380) {
  auto *d = createStyledDialog(parent, minWidth);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(12);
  if (!title.isEmpty()) {
    auto *t = new QLabel(title);
    applyGoldTitleRole(t);
    t->setAlignment(Qt::AlignCenter);
    ly->addWidget(t);
  }
  auto *lb = new QLabel(message);
  applyPopupLabelRole(lb);
  lb->setAlignment(Qt::AlignCenter);
  lb->setWordWrap(true);
  ly->addWidget(lb);
  auto *ok = new QPushButton("OK");
  ok->setMinimumHeight(36);
  applyPrimaryStyle(ok);
  QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
  ly->addWidget(ok);
  centerDialog(d);
  d->exec();
  d->deleteLater();
}

/**
 * @brief Show a success dialog with OK button (green text)
 */
inline void showSuccessDialog(QWidget *parent, const QString &message,
                              int minWidth = 320) {
  auto *d = createStyledDialog(parent, minWidth);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  auto *lb = new QLabel(message);
  applySuccessLabelRole(lb);
  lb->setAlignment(Qt::AlignCenter);
  lb->setWordWrap(true);
  ly->addWidget(lb);
  auto *ok = new QPushButton("OK");
  ok->setMinimumHeight(36);
  applyPrimaryStyle(ok);
  QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
  ly->addWidget(ok);
  centerDialog(d);
  d->exec();
  d->deleteLater();
}

/**
 * @brief Show an error dialog with OK button (red title)
 */
inline void showErrorDialog(QWidget *parent, const QString &message,
                            const QString &title = QString(),
                            int minWidth = 380) {
  auto *d = createStyledDialog(parent, minWidth);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(12);
  if (!title.isEmpty()) {
    auto *t = new QLabel(title);
    applyErrorTitleRole(t);
    t->setAlignment(Qt::AlignCenter);
    ly->addWidget(t);
  }
  auto *lb = new QLabel(message);
  applyPopupLabelRole(lb);
  lb->setAlignment(Qt::AlignCenter);
  lb->setWordWrap(true);
  ly->addWidget(lb);
  auto *ok = new QPushButton("OK");
  ok->setMinimumHeight(36);
  applyPrimaryStyle(ok);
  QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
  ly->addWidget(ok);
  centerDialog(d);
  d->exec();
  d->deleteLater();
}

} // namespace UIHelpers

// ============================================================================
// NEW API: Property-based role setters (Fashion Wars theming system)
//
// These functions set Qt dynamic properties on widgets. The ThemeManager's
// global QSS template uses property selectors to apply themed styles:
//   QPushButton[role="primary"] { background: {{btn.primary.bg}}; ... }
//
// During migration, files switch from:
//   btn->setStyleSheet(UIHelpers::primaryButtonStyle());
// to:
//   UIHelpers::applyPrimaryStyle(btn);
//
// Both APIs coexist during the transition period.
// ============================================================================
namespace UIHelpers {

/// @brief Core: set a "role" property on any widget for QSS matching.
/// Calls unpolish/polish per Qt docs to trigger QSS re-evaluation after
/// dynamic property changes.
inline void applyRole(QWidget *w, const QString &role) {
  w->setProperty("role", role);
  w->style()->unpolish(w);
  w->style()->polish(w);
}

// ---- Button roles ----
inline void applyPrimaryStyle(QPushButton *btn) { applyRole(btn, "primary"); }
inline void applyConfirmStyle(QPushButton *btn) { applyRole(btn, "confirm"); }
inline void applyCancelStyle(QPushButton *btn) { applyRole(btn, "cancel"); }
inline void applyNeutralStyle(QPushButton *btn) { applyRole(btn, "neutral"); }
inline void applyActionStyle(QPushButton *btn) { applyRole(btn, "action"); }
inline void applyCloseStyle(QPushButton *btn) { applyRole(btn, "close"); }
inline void applyApplyDirtyStyle(QPushButton *btn) {
  applyRole(btn, "applyDirty");
}
inline void applyApplyCleanStyle(QPushButton *btn) {
  applyRole(btn, "applyClean");
}
inline void applyNavActiveStyle(QPushButton *btn) {
  applyRole(btn, "navActive");
}
inline void applyNavInactiveStyle(QPushButton *btn) {
  applyRole(btn, "navInactive");
}

// ---- Label roles ----
inline void applyLabelRole(QLabel *lbl) { applyRole(lbl, "label"); }
inline void applyLabelRole(QLabel *lbl, int fontSize) {
  applyRole(lbl, "label");
  QFont f = lbl->font();
  f.setPixelSize(fontSize);
  lbl->setFont(f);
}
inline void applyPageTitleRole(QLabel *lbl) { applyRole(lbl, "pageTitle"); }
inline void applySecondaryRole(QLabel *lbl) { applyRole(lbl, "secondary"); }
inline void applyHintRole(QLabel *lbl) { applyRole(lbl, "hint"); }
inline void applyStatusRole(QLabel *lbl) { applyRole(lbl, "status"); }
inline void applyGoldTitleRole(QLabel *lbl) { applyRole(lbl, "goldTitle"); }
inline void applyErrorTitleRole(QLabel *lbl) { applyRole(lbl, "errorTitle"); }
inline void applySuccessLabelRole(QLabel *lbl) {
  applyRole(lbl, "successLabel");
}
inline void applyPopupLabelRole(QLabel *lbl) { applyRole(lbl, "popupLabel"); }
inline void applyBadgeRole(QLabel *lbl) { applyRole(lbl, "badge"); }
inline void applyProfileBadgePillRole(QLabel *lbl) {
  applyRole(lbl, "profileBadgePill");
}
inline void applyInfoBannerRole(QLabel *lbl) { applyRole(lbl, "infoBanner"); }

// ---- Color roles (apply to any widget) ----
inline void applyWarningColorRole(QWidget *w) { applyRole(w, "warningColor"); }
inline void applySuccessColorRole(QWidget *w) { applyRole(w, "successColor"); }
inline void applyGoldColorRole(QWidget *w) { applyRole(w, "goldColor"); }
inline void applyErrorColorRole(QWidget *w) { applyRole(w, "errorColor"); }

// ---- Widget roles (groupbox, input field) ----
inline void applyGroupBoxRole(QWidget *w) { applyRole(w, "groupBox"); }
inline void applyInputFieldRole(QWidget *w) { applyRole(w, "inputField"); }

// ---- Container roles ----
inline void applyPopupBackgroundRole(QWidget *w) {
  applyRole(w, "popupBackground");
}
inline void applyWindowBackgroundRole(QWidget *w) {
  applyRole(w, "windowBackground");
}
inline void applyContainerRole(QWidget *w) { applyRole(w, "container"); }
inline void applyOverlayRole(QWidget *w) { applyRole(w, "overlay"); }
inline void applyTitleBarRole(QWidget *w) { applyRole(w, "titleBar"); }
inline void applyTitleBarLabelRole(QWidget *w) {
  applyRole(w, "titleBarLabel");
}
inline void applySaveIndicatorRole(QWidget *w) {
  applyRole(w, "saveIndicator");
}
inline void applyTransparentRole(QWidget *w) { applyRole(w, "transparent"); }
inline void applyStyledDialogRole(QDialog *d) { applyRole(d, "styledDialog"); }
inline void applyDialogRole(QDialog *d) { applyRole(d, "dialog"); }

// ============================================================================
// Page Header — unified themed header strip for tab pages
// ============================================================================

/**
 * Creates a themed page header container with background strip, rounded
 * borders, and title label. Returns the container widget; callers can add
 * action buttons to its layout via
 * qobject_cast<QHBoxLayout*>(header->layout()).
 *
 * @param parent  Parent widget
 * @param title   Page title text
 * @param iconName  Icon base name (e.g. "settings"), no path/extension
 * @return Container widget with the header layout
 */
inline QWidget *createPageHeader(QWidget *parent, const QString &title,
                                 const QString &iconName = QString()) {
  auto *container = new QWidget(parent);
  applyRole(container, "pageHeader");
  container->setFixedHeight(
      ThemeManager::instance().activeTheme().layout.pageHeaderHeight);

  auto *layout = new QHBoxLayout(container);
  layout->setContentsMargins(16, 8, 16, 8);
  layout->setSpacing(10);

  if (!iconName.isEmpty()) {
    auto *iconLabel = new QLabel(container);
    setThemedPixmap(iconLabel, iconName, 22);
    iconLabel->setFixedSize(22, 22);
    // REVIEW BEFORE BETA: inline setStyleSheet (uses ThemeManager values)
    iconLabel->setStyleSheet("background: transparent; border: none;");
    layout->addWidget(iconLabel);
  }

  auto *label = new QLabel(title, container);
  applyPageTitleRole(label);
  layout->addWidget(label);
  layout->addStretch();

  return container;
}

} // namespace UIHelpers
