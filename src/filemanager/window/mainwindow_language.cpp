#include "7zip/UI/FileManager/resource.h"
#include "Windows/PropVariant.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include <QEvent>
#include <QToolButton>

void MainWindow::reloadLangItems() {
    if (app_.QtOperations().closed())
        return;
    MyLoadMenu(*menuBar(), true);
    // Qt renders the active primary shortcut. Retain manually described
    // bookmark keys, but remove obsolete resource hints from rebound actions.
    for (QAction *action : findChildren<QAction *>()) {
        if (action->shortcuts().isEmpty() && action->objectName() != "commentAction")
            continue;
        const int tab = action->text().indexOf('\t');
        if (tab >= 0)
            action->setText(action->text().left(tab));
    }
    refreshBookmarks();
    QtReloadLangTexts(*this);
    refreshToolbarTexts();
    for (FileTable *files : findChildren<FileTable *>()) {
        for (int i = 0; i < files->columnCount(); ++i) {
            if (files->isRawProperty(i))
                continue;
            const CPropColumn *column = files->sourceColumn(i);
            const QString label =
                files->propertyId(i) == kpidNoProperty
                    ? QCoreApplication::translate("mainwindow_language", "Order")
                    : QString::fromWCharArray(GetNameOfProperty(
                          files->propertyId(i), column ? column->Name.Ptr() : nullptr));
            files->model()->setHeaderData(i, Qt::Horizontal, label);
        }
        for (int row = 0; row < files->rowCount(); ++row) {
            const QModelIndex index = files->model()->index(row, 0);
            if (index.data(QtPanelList::SourceIndexRole).toUInt() == UInt32(-1))
                files->model()->setData(
                    index, QCoreApplication::translate("mainwindow_language", "Parent folder"),
                    Qt::ToolTipRole);
        }
        files->invalidateLanguageText();
    }
    for (unsigned i = 0; i < kNumPanelsMax; i++) {
        if (auto *root = dynamic_cast<CRootFolder *>(app_.Panels[i].GetFolder())) {
            root->Init();
            if (auto *table = panelUi_[i].table) {
                const bool sorting = table->isSortingEnabled();
                table->setSortingEnabled(false);
                for (int row = 0; row < table->rowCount(); ++row) {
                    const QModelIndex item = table->model()->index(row, 0);
                    NWindows::NCOM::CPropVariant name;
                    if (static_cast<IFolderFolder *>(root)->GetProperty(
                            item.data(QtPanelList::SourceIndexRole).toUInt(), kpidName, &name) ==
                            S_OK &&
                        name.vt == VT_BSTR) {
                        const QString nativeName = QString::fromWCharArray(name.bstrVal);
                        table->model()->setData(item, QVariant(), Qt::DisplayRole);
                        table->model()->setData(item, nativeName, QtPanelList::NameRole);
                    }
                }
                table->setSortingEnabled(sorting);
            }
        }
        auto &panel = app_.Panels[i];
        if (!app_.QtOperations().blocking() && !sourceArchiveProgress_ &&
            panel.ShouldProcessNotifications() && panel.GetFolder() && panel.IsFSDrivesFolder())
            if (auto *address = dynamic_cast<QtPanelAddressCombo *>(panelUi_[i].pathCombo))
                address->ReloadCurrentPath();
        if (panelUi_[i].statusLabels[0])
            app_.Panels[i].Refresh_StatusBar();
    }
}

void MainWindow::refreshToolbarTexts() {
    auto *toolbar = findChild<QToolBar *>("archiveToolbar");
    if (!toolbar)
        return;
    // Original AddButton and WM_NOTIFY/TTN_GETDISPINFO share SetButtonText.
    // QAction keeps its File menu caption; Qt supplies button/tooltip lifetime.
    for (QAction *action : {toolbarAdd_, extract_, test_, copy_, move_, remove_, infoAction_})
        if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action))) {
            UString text;
            SetButtonText(action->property("nativeCommandID").toInt(), text);
            if (text.IsEmpty())
                continue;
            const QString caption = action == extract_
                                        ? QCoreApplication::translate("ArchiveRedesign", "Extract")
                                        : QString::fromWCharArray(text);
            button->setText(caption);
            button->setAccessibleName(caption);
            const QString shortcut =
                (action == infoAction_ ? properties_->shortcut() : action->shortcut())
                    .toString(QKeySequence::NativeText);
            button->setToolTip(
                shortcut.isEmpty() ? caption : QStringLiteral("%1 (%2)").arg(caption, shortcut));
        }
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && menuBar() && findChild<QMenu *>("fileMenu"))
        reloadLangItems();
    QMainWindow::changeEvent(event);
}
