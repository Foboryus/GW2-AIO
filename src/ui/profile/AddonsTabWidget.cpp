#include "AddonsTabWidget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/ProfileEditor.h" // For AccountProfile
#include "ui/UIHelpers.h"

AddonsTabWidget::AddonsTabWidget(AccountProfile &profile, QWidget *parent)
    : QWidget(parent), m_profile(profile) {
  setupUI();
}

void AddonsTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);

  auto *info = new QLabel(
      "Addon DLLs\n\n"
      "DLLs listed here will be injected when launching this profile.\n"
      "Common addons: ArcDPS (d3d11.dll), GW2Radial, etc.");
  info->setWordWrap(true);
  UIHelpers::applyHintRole(info);
  info->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));
  layout->addWidget(info);

  m_dllList = new QListWidget();
  layout->addWidget(m_dllList);

  auto *btnLayout = new QHBoxLayout();

  auto *addBtn = new QPushButton(UIHelpers::themedIcon("plus"), "Add DLL");
  UIHelpers::applyNeutralStyle(addBtn);
  connect(addBtn, &QPushButton::clicked, this, &AddonsTabWidget::onAddDll);
  btnLayout->addWidget(addBtn);

  auto *removeBtn = new QPushButton(UIHelpers::themedIcon("trash"), "Remove");
  UIHelpers::applyCancelStyle(removeBtn);
  connect(removeBtn, &QPushButton::clicked, this,
          &AddonsTabWidget::onRemoveDll);
  btnLayout->addWidget(removeBtn);

  btnLayout->addStretch();
  layout->addLayout(btnLayout);
}

void AddonsTabWidget::load() {
  m_dllList->clear();
  for (const QString &dll : m_profile.injectedDlls) {
    m_dllList->addItem(dll);
  }
}

void AddonsTabWidget::save() {
  m_profile.injectedDlls.clear();
  for (int i = 0; i < m_dllList->count(); i++) {
    m_profile.injectedDlls.append(m_dllList->item(i)->text());
  }
}

void AddonsTabWidget::onAddDll() {
  QString dll = QFileDialog::getOpenFileName(this, "Select DLL", QString(),
                                             "DLL Files (*.dll)");
  if (!dll.isEmpty()) {
    m_dllList->addItem(dll);
    emit modified();
  }
}

void AddonsTabWidget::onRemoveDll() {
  auto *current = m_dllList->currentItem();
  if (current) {
    delete current;
    emit modified();
  }
}
