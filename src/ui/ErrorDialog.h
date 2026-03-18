#pragma once

#include "UIHelpers.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

/**
 * @brief GW2-styled error dialog — frameless, themed via UIHelpers
 *
 * Follows dev standard: frameless, translucent, gold border, SVG icons only.
 */
class ErrorDialog : public QDialog {
  Q_OBJECT

public:
  enum class Type { Info, Warning, Error, Critical };

  explicit ErrorDialog(Type type, const QString &title, const QString &message,
                       QWidget *parent = nullptr);

  /**
   * @brief Set additional details (expandable)
   */
  void setDetails(const QString &details);

  /**
   * @brief Show a quick error dialog
   */
  static void showError(const QString &title, const QString &message,
                        QWidget *parent = nullptr);

  static void showWarning(const QString &title, const QString &message,
                          QWidget *parent = nullptr);

  static void showInfo(const QString &title, const QString &message,
                       QWidget *parent = nullptr);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void setupUI(Type type, const QString &title, const QString &message);

  QTextEdit *m_detailsEdit = nullptr;
  QPushButton *m_detailsButton = nullptr;
  bool m_detailsVisible = false;
  QPoint m_dragPos;
  bool m_dragging = false;
};

// Implementation
inline ErrorDialog::ErrorDialog(Type type, const QString &title,
                                const QString &message, QWidget *parent)
    : QDialog(parent) {
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setupUI(type, title, message);
}

inline void ErrorDialog::setupUI(Type type, const QString &title,
                                 const QString &message) {
  setMinimumWidth(400);

  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  auto *bgContainer = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *mainLayout = new QVBoxLayout(bgContainer);
  mainLayout->setContentsMargins(16, 12, 16, 16);
  mainLayout->setSpacing(16);

  // Title bar with icon based on type
  QString iconPath;
  QString titleText = title;
  switch (type) {
  case Type::Info:
    iconPath = ":/icons/check-circle.svg";
    break;
  case Type::Warning:
    iconPath = ":/icons/alert-yellow.svg";
    break;
  case Type::Error:
    iconPath = ":/icons/x-circle.svg";
    break;
  case Type::Critical:
    iconPath = ":/icons/circle-red.svg";
    break;
  }

  auto *titleBar = UIHelpers::createTitleBar(bgContainer, titleText, iconPath,
                                             [this]() { reject(); });
  mainLayout->addWidget(titleBar);

  // Message label
  auto *messageLabel = new QLabel(message);
  messageLabel->setWordWrap(true);
  UIHelpers::applyPopupLabelRole(messageLabel);
  mainLayout->addWidget(messageLabel);

  // Details section (hidden by default)
  m_detailsEdit = new QTextEdit();
  m_detailsEdit->setReadOnly(true);
  m_detailsEdit->setVisible(false);
  m_detailsEdit->setMaximumHeight(150);
  m_detailsEdit->setStyleSheet(
      QString("font-family: Consolas; font-size: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  mainLayout->addWidget(m_detailsEdit);

  // Buttons
  QHBoxLayout *buttonLayout = new QHBoxLayout();

  m_detailsButton = new QPushButton("Show Details");
  UIHelpers::applyNeutralStyle(m_detailsButton);
  m_detailsButton->setVisible(false);
  connect(m_detailsButton, &QPushButton::clicked, this, [this]() {
    m_detailsVisible = !m_detailsVisible;
    m_detailsEdit->setVisible(m_detailsVisible);
    m_detailsButton->setText(m_detailsVisible ? "Hide Details"
                                              : "Show Details");
    adjustSize();
  });
  buttonLayout->addWidget(m_detailsButton);

  buttonLayout->addStretch();

  QPushButton *okButton = new QPushButton("OK");
  okButton->setDefault(true);
  okButton->setMinimumWidth(100);
  UIHelpers::applyPrimaryStyle(okButton);
  connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(okButton);

  mainLayout->addLayout(buttonLayout);

  UIHelpers::centerDialog(this);
}

inline void ErrorDialog::setDetails(const QString &details) {
  m_detailsEdit->setPlainText(details);
  m_detailsButton->setVisible(!details.isEmpty());
}

inline void ErrorDialog::showError(const QString &title, const QString &message,
                                   QWidget *parent) {
  ErrorDialog dialog(Type::Error, title, message, parent);
  dialog.exec();
}

inline void ErrorDialog::showWarning(const QString &title,
                                     const QString &message, QWidget *parent) {
  ErrorDialog dialog(Type::Warning, title, message, parent);
  dialog.exec();
}

inline void ErrorDialog::showInfo(const QString &title, const QString &message,
                                  QWidget *parent) {
  ErrorDialog dialog(Type::Info, title, message, parent);
  dialog.exec();
}

// --- Drag support (frameless window) ---
inline void ErrorDialog::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && event->position().y() < 50) {
    m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
    m_dragging = true;
    event->accept();
  } else {
    m_dragging = false;
  }
}

inline void ErrorDialog::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPos);
    event->accept();
  }
}

inline void ErrorDialog::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
  }
  QDialog::mouseReleaseEvent(event);
}
