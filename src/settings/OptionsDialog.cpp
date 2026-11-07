// Adapted from 7-Zip OptionsDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt task pages retain the source state/apply owners.
#include "7zip/UI/FileManager/resource.h"
#include "localization/LangUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/associations/qtassociationplatform.h"
#include "settings/FoldersPage.h"
#include "settings/LangPage.h"
#include "settings/MenuPage.h"
#include "settings/SettingsPage.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "settings/qtoptionspages.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

QtFmOptionsDialog::QtFmOptionsDialog(const QString &settingsPath, QWidget *parent,
                                     IFolderManager *manager,
                                     std::shared_ptr<IQtAssociationServices> associations)
    : QDialog(parent) {
    setObjectName("settingsDialog");
    ReloadLang(settingsPath);
    QtBindLangText(this, IDS_OPTIONS, "Options");
    auto *pages = new QtOptionsPages(this);
    _sections = pages;
    pages->setObjectName("optionsPages");
    pages->tabBar()->setFocusPolicy(Qt::StrongFocus); // Native property-sheet WS_TABSTOP.
    _foldersPage = new CFoldersPage(settingsPath, this);
    _settingsPage = new CSettingsPage(settingsPath, this, true);
    _appliedColorScheme = _settingsPage->settings().ColorScheme;
    _menuPage = new CMenuPage(settingsPath, this);
    _langPage = new CLangPage(settingsPath, this);
    if (!associations)
        associations = CreateQtAssociationServices(CreateNativeAssociationPlatform());
    _systemPage = new CSystemPage(manager, std::move(associations), this);
    auto *buttons = new QtLangButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->SetAcceptText("SettingsRedesign", QT_TRANSLATE_NOOP("SettingsRedesign", "Save"));
    _buttons = buttons;
    _buttons->setObjectName("settingsButtons");
    buildSections();
    reloadLangItems();
    // Keep existing source tab stops under QMacStyle's restricted button policy.
    for (auto *button : findChildren<QAbstractButton *>())
        if (button->focusPolicy() & Qt::TabFocus)
            button->setFocusPolicy(Qt::StrongFocus);
    connect(_buttons, &QDialogButtonBox::accepted, this, &QtFmOptionsDialog::accept);
    connect(_buttons, &QDialogButtonBox::rejected, this, &QtFmOptionsDialog::reject);
    connect(_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &QtFmOptionsDialog::apply);
    connect(_settingsPage, &CSettingsPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
    connect(_foldersPage, &CFoldersPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
    connect(_menuPage, &CMenuPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
    connect(_langPage, &CLangPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
    connect(_systemPage, &CSystemPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
    updateApplyButton();
    resize(QSize(fontMetrics().averageCharWidth() * 95, fontMetrics().lineSpacing() * 32)
               .expandedTo(minimumSizeHint()));
    pages->focusInitialControl();
}

bool QtFmOptionsDialog::apply() {
    if (_applying)
        return false;
    QPointer<QtFmOptionsDialog> alive(this);
    _applying = true;
    const auto restore = qScopeGuard([alive] {
        if (alive) {
            alive->setEnabled(true);
            if (alive)
                alive->_applying = false;
        }
    });
    setEnabled(false);
    const LONG associationError = _systemPage->OnApply();
    setEnabled(true);
    if (associationError != 0) {
        const QString message =
            associationError == 1237
                ? QCoreApplication::translate("OptionsDialog",
                                              "An association changed outside the application. "
                                              "Review the refreshed state and try again.")
                : QCoreApplication::translate("OptionsDialog",
                                              "Unable to change the association (error %1).")
                      .arg(associationError);
        QMessageBox::critical(this, QCoreApplication::translate("OptionsDialog", "7-Zip"), message);
    }
    // Source SystemPage reports an association error and returns
    // PSNRET_NOERROR, so the property sheet still applies the other pages.
    _menuPage->OnApply();
    if (_foldersPage->OnApply() != 0) {
        auto *path = findChild<QWidget *>("workPath");
        revealControl(path->isEnabled() ? path : findChild<QWidget *>("workSpecified"));
        updateApplyButton();
        return false;
    }
    if (_settingsPage->OnApply() != 0) {
        revealControl(findChild<QWidget *>("extractMemorySpin"));
        updateApplyButton();
        return false;
    }
    const auto colorScheme = _settingsPage->settings().ColorScheme;
    if (colorScheme != _appliedColorScheme) {
        QGuiApplication::styleHints()->setColorScheme(colorScheme);
        _appliedColorScheme = colorScheme;
    }
    const bool languageChanged = _langPage->IsModified();
    _langPage->OnApply();
    if (languageChanged)
        QtNotifyLanguageChanged();
    emit Applied();
    if (!alive)
        return false;
    updateApplyButton();
    // Applied observers can leave a new draft after the pages were saved.
    return !hasPendingChanges();
}
bool QtFmOptionsDialog::hasPendingChanges() const {
    return _foldersPage->IsModified() || _settingsPage->IsModified() || _menuPage->IsModified() ||
           _langPage->IsModified() || _systemPage->IsModified();
}
void QtFmOptionsDialog::updateApplyButton() {
    _buttons->button(QDialogButtonBox::Apply)->setEnabled(hasPendingChanges());
}
void QtFmOptionsDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _langPage)
        reloadLangItems();
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::FontChange ||
        event->type() == QEvent::PaletteChange)
        updateNavigationGeometry();
}
void QtFmOptionsDialog::accept() {
    QPointer<QtFmOptionsDialog> alive(this);
    if (apply() && alive)
        QDialog::accept();
}
void QtFmOptionsDialog::reject() {
    if (!_applying)
        QDialog::reject();
}
void QtFmOptionsDialog::closeEvent(QCloseEvent *event) {
    if (_applying)
        event->ignore();
    else
        QDialog::closeEvent(event);
}
