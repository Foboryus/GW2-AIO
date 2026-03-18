#include "ArgumentsTabWidget.h"
#include "ui/UIHelpers.h"

#include <QDesktopServices>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "ui/ProfileEditor.h" // For LaunchArg struct and AccountProfile
#include "ui/ToggleSwitch.h"

ArgumentsTabWidget::ArgumentsTabWidget(AccountProfile &profile,
                                       const QList<LaunchArg> &standardArgs,
                                       QWidget *parent)
    : QWidget(parent), m_profile(profile), m_standardArgs(standardArgs) {
  setupUI();
}

void ArgumentsTabWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *info = new QLabel("Toggle launch arguments for this profile:");
  UIHelpers::applyHintRole(info);
  mainLayout->addWidget(info);

  // Scrollable area for toggles
  auto *scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *argsWidget = new QWidget();
  auto *argsLayout = new QVBoxLayout(argsWidget);
  argsLayout->setSpacing(8);

  for (const auto &arg : m_standardArgs) {
    auto *toggle =
        new LabeledToggle(QString("%1  (%2)").arg(arg.arg, arg.desc));
    toggle->setProperty("arg", arg.arg);
    m_argToggles.append(toggle);
    argsLayout->addWidget(toggle);

    // Emit modified signal when any toggle changes
    connect(toggle, &LabeledToggle::toggled, this,
            &ArgumentsTabWidget::modified);
  }

  argsLayout->addStretch();
  scrollArea->setWidget(argsWidget);
  mainLayout->addWidget(scrollArea);

  // Wiki button
  auto *wikiBtn = new QPushButton(UIHelpers::themedIcon("book"),
                                  "View all arguments on GW2 Wiki");
  UIHelpers::applyNeutralStyle(wikiBtn);
  connect(wikiBtn, &QPushButton::clicked, []() {
    QDesktopServices::openUrl(
        QUrl("https://wiki.guildwars2.com/wiki/Command_line_arguments"));
  });
  mainLayout->addWidget(wikiBtn);
}

void ArgumentsTabWidget::load() {
  // Set toggles based on profile's argument list
  for (auto *toggle : m_argToggles) {
    QString arg = toggle->property("arg").toString();
    toggle->blockSignals(true);
    toggle->setChecked(m_profile.arguments.contains(arg));
    toggle->blockSignals(false);
  }
}

void ArgumentsTabWidget::save() {
  // Build arguments list from checked toggles
  m_profile.arguments.clear();
  for (auto *toggle : m_argToggles) {
    if (toggle->isChecked()) {
      m_profile.arguments.append(toggle->property("arg").toString());
    }
  }
}
