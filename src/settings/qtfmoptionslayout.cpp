#include "localization/qtlangcontrols.h"
#include "settings/EditPage.h"
#include "settings/FoldersPage.h"
#include "settings/LangPage.h"
#include "settings/MenuPage.h"
#include "settings/SettingsPage.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "ui/controls/qtdisclosure.h"
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QVBoxLayout *fields(QWidget *widget) {
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    return layout;
}
QWidget *memoryPage(CSettingsPage *owner) {
    auto *body = owner->memoryControls();
    delete body->layout(); // The source controls and their connections survive.
    auto *layout = fields(body);
    auto *intro = body->findChild<QLabel *>("extractMemoryLabel");
    QtBindLocalText(
        intro, "SettingsRedesign",
        QT_TRANSLATE_NOOP("SettingsRedesign", "Limit memory when extracting large archives."));
    layout->addWidget(intro);
    intro->setForegroundRole(QPalette::PlaceholderText);
    auto *enable = body->findChild<QCheckBox *>("extractMemorySet");
    enable->setAccessibleName({});
    QtBindLocalText(enable, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Set a memory limit"));
    layout->addWidget(enable);
    auto *dependent = new QWidget(body);
    auto *values = new QVBoxLayout(dependent);
    values->setContentsMargins(24, 0, 0, 0);
    values->setSpacing(8);
    auto *label = new QLabel(dependent);
    QtBindLocalText(label, "SettingsRedesign", QT_TRANSLATE_NOOP("SettingsRedesign", "Limit, GB"));
    auto *spin = body->findChild<QAbstractSpinBox *>("extractMemorySpin");
    label->setBuddy(spin);
    values->addWidget(label);
    values->addWidget(spin, 0, Qt::AlignLeading);
    auto *ram = body->findChild<QLabel *>("extractMemoryRam");
    const auto ramValues = ram->property("languageArguments").toStringList();
    if (!ramValues.isEmpty())
        QtBindLocalText(ram, "SettingsRedesign",
                        QT_TRANSLATE_NOOP("SettingsRedesign", "This computer: %1 GB"), false,
                        ramValues);
    ram->setVisible(!ramValues.isEmpty());
    ram->setForegroundRole(QPalette::PlaceholderText);
    values->addWidget(ram);
    layout->addWidget(dependent);
    layout->addWidget(body->findChild<QCheckBox *>("largePagesOption"));
    return body;
}
QWidget *listPage(CSettingsPage *owner) {
    auto *body = owner->listControls();
    delete body->layout();
    auto *layout = fields(body);
    for (int group = 0; group < 2; ++group) {
        auto *part = new QWidget(body);
        auto *rows = fields(part);
        rows->setSpacing(8);
        auto *heading = new QLabel(part);
        auto font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        QtBindLocalText(heading, "SettingsRedesign",
                        group == 0 ? QT_TRANSLATE_NOOP("SettingsRedesign", "Display")
                                   : QT_TRANSLATE_NOOP("SettingsRedesign", "Controls"));
        rows->addWidget(heading);
        for (const QString &name :
             group == 0 ? QStringList{"showDotsOption", "realFileIconsOption", "fullRowOption",
                                      "gridOption"}
                        : QStringList{"singleClickOption", "alternativeSelectionOption",
                                      "showSystemMenuOption"})
            rows->addWidget(body->findChild<QCheckBox *>(name));
        layout->addWidget(part);
    }
    return body;
}
void section(QTabWidget *pages, const char *name, const char *title, QWidget *body,
             const char *intro = nullptr, const char *hint = nullptr) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(20);
    auto *heading = new QLabel(page);
    heading->setObjectName("settingsHeading");
    heading->setWordWrap(true);
    auto font = heading->font();
    font.setBold(true);
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * 1.25);
    heading->setFont(font);
    QtBindLocalText(heading, "SettingsRedesign", title);
    layout->addWidget(heading);
    const auto description = [page, layout](const char *text) {
        auto *label = new QLabel(page);
        label->setWordWrap(true);
        label->setForegroundRole(QPalette::PlaceholderText);
        QtBindLocalText(label, "SettingsRedesign", text);
        layout->addWidget(label);
    };
    if (intro)
        description(intro);
    if (body->layout())
        body->layout()->setContentsMargins(0, 0, 0, 0);
#ifdef Q_OS_MACOS
    // Cocoa's layout item excludes button overhang; the scroll body must also
    // contain that native frame at its last row.
    for (auto *button : body->findChildren<QAbstractButton *>())
        button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->addWidget(body);
    if (hint)
        description(hint);
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setObjectName(name);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    page->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setProperty("sectionTitle", title);
    pages->addTab(scroll, QCoreApplication::translate("SettingsRedesign", title));
}
} // namespace

void QtFmOptionsDialog::buildSections() {
    auto *general = new QWidget;
    auto *basic = fields(general);
    basic->addWidget(_settingsPage->appearanceControls());
    _settingsPage->setFocusProxy(nullptr);
    _settingsPage->hide();
    _langPage->layout()->setContentsMargins(0, 0, 0, 0);
    auto *info = _langPage->findChild<QPlainTextEdit *>("languageInfo");
    _langPage->layout()->removeWidget(info);
    auto *metadata = new QWidget;
    auto *metadataLayout = fields(metadata);
    metadataLayout->setContentsMargins(24, 0, 0, 0);
    metadataLayout->addWidget(info);
    info->setMinimumHeight(info->fontMetrics().lineSpacing() * 4);
    _langPage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    basic->addWidget(_langPage);
    QtBindLocalText(_langPage->findChild<QLabel *>("languageLabel"), "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Interface language"));
    auto *translation = new QWidget(general);
    auto *translationLayout = fields(translation);
    auto *description = new QLabel(translation);
    description->setObjectName("translationDescription");
    description->setWordWrap(true);
    description->setContentsMargins(24, 0, 0, 0);
    description->setForegroundRole(QPalette::PlaceholderText);
    QtBindLocalText(
        description, "SettingsRedesign",
        QT_TRANSLATE_NOOP("SettingsRedesign", "Information about the installed translation"));
    _translationGroup = new QtDisclosure(metadata, translation, description);
    _translationGroup->setObjectName("translationDetails");
    QtBindLocalText(_translationGroup->button(), "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "About translation"));
    translationLayout->addWidget(_translationGroup);
    basic->addWidget(translation);
    section(_sections, "generalSection", QT_TRANSLATE_NOOP("SettingsRedesign", "General"), general);

    _systemPage->SimplifyPresentation();
    if (_systemPage->IntegrationAvailable())
        section(_sections, "fileOpeningSection",
                QT_TRANSLATE_NOOP("SettingsRedesign", "Opening files"), _systemPage);
    else
        _systemPage->hide();
    section(_sections, "memorySection", QT_TRANSLATE_NOOP("SettingsRedesign", "Memory"),
            memoryPage(_settingsPage));
    section(_sections, "temporarySection", QT_TRANSLATE_NOOP("SettingsRedesign", "Temporary files"),
            _foldersPage,
            QT_TRANSLATE_NOOP("SettingsRedesign",
                              "Choose where to keep temporary files when working with archives."));
    section(_sections, "applicationsSection", QT_TRANSLATE_NOOP("SettingsRedesign", "Applications"),
            _editPage,
            QT_TRANSLATE_NOOP("SettingsRedesign",
                              "Applications for viewing and editing archived files."),
            QT_TRANSLATE_NOOP("SettingsRedesign",
                              "You can specify an application and command-line arguments."));
    section(_sections, "fileListSection", QT_TRANSLATE_NOOP("SettingsRedesign", "File list"),
            listPage(_settingsPage));
    if (_menuPage->IntegrationSupported())
        section(_sections, "shellMenuSection",
                QT_TRANSLATE_NOOP("SettingsRedesign", "File Explorer commands"), _menuPage);
    else
        _menuPage->hide();
    buildNavigation();
    refreshSectionSummaries();
}

void QtFmOptionsDialog::refreshSectionSummaries() {
    _translationGroup->setSummary(_langPage->IsModified()
                                      ? QCoreApplication::translate("SettingsRedesign", "Changed")
                                      : QString());
}
