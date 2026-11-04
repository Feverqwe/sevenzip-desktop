#pragma once
#include "settings/RegistryUtils.h"
#include <QDialog>
#include <QPointer>
#include <memory>
struct IFolderManager;
class IQtAssociationServices;
class CSystemPage;
class CEditPage;
class CFoldersPage;
class CSettingsPage;
class CMenuPage;
class CLangPage;
class QDialogButtonBox;
class QTabWidget;
class QtDisclosure;
class QListWidget;
class QGridLayout;
class QFrame;

// Task pages retain the six source state/apply owners.
class QtFmOptionsDialog final : public QDialog {
    Q_OBJECT
    CEditPage *_editPage;
    CFoldersPage *_foldersPage;
    CSettingsPage *_settingsPage;
    CMenuPage *_menuPage;
    CLangPage *_langPage = nullptr;
    CSystemPage *_systemPage;
    bool _applying = false;
    Qt::ColorScheme _appliedColorScheme = Qt::ColorScheme::Unknown;
    QDialogButtonBox *_buttons;
    QDialogButtonBox *_helpButtons;
    QWidget *_footer;
    QTabWidget *_sections;
    QPointer<QWidget> _helpContext;
    QListWidget *_navigation;
    QListWidget *_compactNavigation;
    QWidget *_compactNavigationPanel;
    QWidget *_navigationPanel;
    QFrame *_navigationDivider;
    QGridLayout *_frame = nullptr;
    bool _compact = false;
    QtDisclosure *_translationGroup;
    void buildSections();
    void buildNavigation();
    void updateNavigationGeometry();
    void refreshSectionSummaries();
    void revealControl(QWidget *control);
    bool hasPendingChanges() const;
    void updateApplyButton();
    void changeEvent(QEvent *event) override;
    void reloadLangItems();
    void showPageHelp();
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  public:
    explicit QtFmOptionsDialog(const QString &settingsPath, QWidget *parent = nullptr,
                               IFolderManager *manager = nullptr,
                               std::shared_ptr<IQtAssociationServices> associations = {});
    // Returns whether the dialog can close after applying.
    bool apply();
    void accept() override;
    void reject() override;

  signals:
    void Applied();
};
