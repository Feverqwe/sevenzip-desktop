// Adapted from 7-Zip OptionsDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt task pages retain the original six state/apply owners.
#include "7zip/UI/FileManager/resource.h"
#include "localization/LangUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/associations/qtassociationplatform.h"
#include "settings/EditPage.h"
#include "settings/FoldersPage.h"
#include "settings/LangPage.h"
#include "settings/MenuPage.h"
#include "settings/SettingsPage.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
// The native property sheet activates the new page's first tab stop after
// Ctrl+Tab, rather than retaining a previous Qt child or focusing the tab bar.
class QtOptionsPages final : public QTabWidget {
  public:
    explicit QtOptionsPages(QWidget *parent) : QTabWidget(parent) {
        const Qt::KeyboardModifier controls[] = {
            Qt::ControlModifier,
#ifdef Q_OS_MACOS
            Qt::MetaModifier, // Physical Control under Cocoa's default modifier mapping.
#endif
        };
        for (const auto control : controls)
            for (const bool reverse : {false, true}) {
                const auto shift = reverse ? Qt::ShiftModifier : Qt::NoModifier;
                auto *shortcut = new QShortcut(
                    QKeySequence(QKeyCombination(control | shift, Qt::Key_Tab)), this);
                connect(shortcut, &QShortcut::activated, this, [this, shift] {
                    // Window context includes the footer. Keep Qt's own
                    // enabled/visible-page selection and our source focus rule.
                    QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier | shift);
                    keyPressEvent(&event);
                });
            }
    }
    void focusInitialControl() {
        QWidget *page = currentWidget();
        if (page) {
            for (QWidget *control = page->nextInFocusChain(); control != page;
                 control = control->nextInFocusChain()) {
                if (!page->isAncestorOf(control) || !control->isVisibleTo(page) ||
                    !control->isEnabled() || !(control->focusPolicy() & Qt::TabFocus))
                    continue;
                if (auto *radio = qobject_cast<QRadioButton *>(control);
                    radio && radio->group() && radio->group()->exclusive()) {
                    if (auto *checked = radio->group()->checkedButton();
                        checked && checked->isEnabled())
                        control = checked;
                }
                QPointer<QWidget> target(control);
                control->setFocus(Qt::OtherFocusReason);
                if (auto *edit = qobject_cast<QLineEdit *>(target.data()))
                    edit->selectAll();
                return;
            }
        }
        for (const char *name : {"settingsNavigation", "settingsSectionChoice"})
            if (auto *navigation = parentWidget()->findChild<QListWidget *>(name);
                navigation && navigation->isVisible() && navigation->isEnabled()) {
                navigation->setFocus(Qt::TabFocusReason);
                return;
            }
    }

  private:
    void keyPressEvent(QKeyEvent *event) override {
        QPointer<QTabWidget> alive(this);
        const int before = currentIndex();
        const bool cycle = event->matches(QKeySequence::NextChild) ||
                           event->matches(QKeySequence::PreviousChild) ||
                           ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
                            (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)));
        QTabWidget::keyPressEvent(event); // Qt owns page selection and platform key bindings.
        if (alive && cycle && event->isAccepted() && currentIndex() != before)
            focusInitialControl();
    }
};
} // namespace

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
    _editPage = new CEditPage(settingsPath, this);
    _foldersPage = new CFoldersPage(settingsPath, this);
    _settingsPage = new CSettingsPage(settingsPath, this, true);
    _appliedColorScheme = _settingsPage->settings().ColorScheme;
    _menuPage = new CMenuPage(settingsPath, this);
    _langPage = new CLangPage(settingsPath, this);
    if (!associations)
        associations = CreateQtAssociationServices(CreateNativeAssociationPlatform());
    _systemPage = new CSystemPage(manager, std::move(associations), this);
    // Retain source owner IDs for help/resource provenance, not navigation.
    const unsigned pageIDs[] = {2200, 2300, 2400, 2103, 2500, 2101};
    QWidget *pagePointers[] = {_systemPage, _menuPage,     _foldersPage,
                               _editPage,   _settingsPage, _langPage};
    for (unsigned i = 0; i < 6; i++) {
        pagePointers[i]->setProperty("sourcePageID", pageIDs[i]);
    }
    auto *helpButtons = new QtLangButtonBox(QDialogButtonBox::Help, this);
    helpButtons->UseCompactHelp();
    helpButtons->button(QDialogButtonBox::Help)->setObjectName("settingsHelp");
    _helpButtons = helpButtons;
    auto *buttons = new QtLangButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->SetAcceptText("SettingsRedesign", QT_TRANSLATE_NOOP("SettingsRedesign", "Save"));
    _buttons = buttons;
    _buttons->setObjectName("settingsButtons");
    buildSections();
    reloadLangItems();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && _sections->isAncestorOf(now))
            _helpContext = now;
    });
    // Keep existing source tab stops under QMacStyle's restricted button policy.
    for (auto *button : findChildren<QAbstractButton *>())
        if (button->focusPolicy() & Qt::TabFocus)
            button->setFocusPolicy(Qt::StrongFocus);
    connect(_buttons, &QDialogButtonBox::accepted, this, &QtFmOptionsDialog::accept);
    connect(_buttons, &QDialogButtonBox::rejected, this, &QtFmOptionsDialog::reject);
    connect(_helpButtons, &QDialogButtonBox::helpRequested, this, &QtFmOptionsDialog::showPageHelp);
    auto *help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, &QtFmOptionsDialog::showPageHelp);
    connect(_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &QtFmOptionsDialog::apply);
    connect(_editPage, &CEditPage::Changed, this, &QtFmOptionsDialog::updateApplyButton);
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

void QtFmOptionsDialog::showPageHelp() {
    QWidget *context = focusWidget();
    if (!context || !_sections->isAncestorOf(context))
        context = _helpContext;
    const auto inside = [context](QWidget *owner) { return owner && owner->isAncestorOf(context); };
    const auto page = _sections->currentWidget()->property("sourcePageID").toUInt();
    if (page == 2103)
        _editPage->OnNotifyHelp();
    else if (page == 2400)
        _foldersPage->OnNotifyHelp();
    else if (page == 2500 || (page == 2101 && inside(_settingsPage->appearanceControls())))
        _settingsPage->OnNotifyHelp();
    else if (page == 2300)
        _menuPage->OnNotifyHelp();
    else if (page == 2101)
        _langPage->OnNotifyHelp();
    else if (page == 2200)
        _systemPage->OnNotifyHelp();
    else
        _settingsPage->OnNotifyHelp();
}
bool QtFmOptionsDialog::apply() {
    if (_applying)
        return false;
    QScopedValueRollback<bool> applying(_applying, true);
    setEnabled(false);
    const auto enable = qScopeGuard([this] { setEnabled(true); });
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
    _foldersPage->OnApply();
    _editPage->OnApply();
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
    updateApplyButton();
    // Applied observers can leave a new draft after the pages were saved.
    return !hasPendingChanges();
}
bool QtFmOptionsDialog::hasPendingChanges() const {
    return _editPage->IsModified() || _foldersPage->IsModified() || _settingsPage->IsModified() ||
           _menuPage->IsModified() || _langPage->IsModified() || _systemPage->IsModified();
}
void QtFmOptionsDialog::updateApplyButton() {
    _buttons->button(QDialogButtonBox::Apply)->setEnabled(hasPendingChanges());
    refreshSectionSummaries();
}
void QtFmOptionsDialog::reloadLangItems() {
    QtReloadLangTexts(*this);
    _menuPage->ReloadLangItems();
    _systemPage->ReloadLangItems();
    auto *pages = findChild<QTabWidget *>("optionsPages");
    for (int i = 0; i < pages->count(); i++) {
        pages->setTabText(
            i, QCoreApplication::translate(
                   "SettingsRedesign",
                   pages->widget(i)->property("sectionTitle").toByteArray().constData()));
        _navigation->item(i)->setText(pages->tabText(i));
        _compactNavigation->item(i)->setText(pages->tabText(i));
    }
    refreshSectionSummaries();
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
    if (apply())
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
