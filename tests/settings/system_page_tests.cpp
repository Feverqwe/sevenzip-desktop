#include "Common/MyCom.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QColor>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QImage>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {
QIcon SolidIcon(Qt::GlobalColor color) {
    QPixmap pixmap(16, 16);
    pixmap.fill(color);
    return QIcon(pixmap);
}
QColor IconColor(const QTreeWidgetItem &item) {
    const QImage sample = item.icon(0).pixmap(1, 1).toImage();
    return sample.isNull() ? QColor() : sample.pixelColor(0, 0);
}

class CFixtureManager final : public IFolderManager, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderManager)
  public:
    Z7_COM7F_IMF(OpenFolderFile(IInStream *, const wchar_t *, const wchar_t *, IFolderFolder **,
                                IProgress *))
    override { return E_NOTIMPL; }
    Z7_COM7F_IMF(GetExtensions(BSTR *extensions)) override {
        return StringToBstr(UString(L"zip 7z ZIP тар"), extensions);
    }
    Z7_COM7F_IMF(GetIconPath(const wchar_t *, BSTR *path, Int32 *index)) override {
        *index = 0;
        return StringToBstr(UString(L"/app/sevenzip"), path);
    }
};
class CFakeAssociations final : public IQtAssociationServices {
  public:
    bool permitted[2] = {true, true};
    bool unsupportedUnicodeFormat = false;
    bool externallyChanged = false;
    bool nativeAliases = false;
    bool associated = false;
    LONG failure = 0;
    int added = 0;
    int deleted = 0;
    int notifications = 0;
    QString userName;
    QIcon ownIcon;
    QIcon originalIcon;
    QIcon externalIcon;
    bool CanApply(unsigned g) const override { return permitted[g]; }
    bool CanApplyFormat(unsigned g, const UString &ext) const override {
        return permitted[g] && !(unsupportedUnicodeFormat && ext == L"тар");
    }
    QString UnavailableReason(unsigned) const override { return "Permission denied"; }
    QString CurrentUserName() const override { return userName; }
    bool RefreshNativeStateAfterApply() const override { return nativeAliases; }
    UString OwnIconPath() const override { return UString(L"/app/sevenzip"); }
    QIcon OwnIcon() const override { return ownIcon; }
    QIcon AssociationIcon(unsigned, const UString &) const override {
        return externallyChanged ? externalIcon : originalIcon;
    }
    bool Read(unsigned, const UString &ext, NRegistryAssoc::CShellExtInfo &info) override {
        if (nativeAliases) {
            info.ProgramKey = associated ? "7-Zip.native" : "Native.Other";
            info.IconPath = associated ? OwnIconPath() : UString(L"/other");
            return true;
        }
        info.ProgramKey = externallyChanged ? "External.Archiver"
                          : ext == L"zip"   ? "Other.Archiver"
                                            : "";
        return true;
    }
    LONG Add(unsigned, const UString &, const NRegistryAssoc::CShellExtInfo &, const UString &,
             const UString &command, const UString &, int) override {
        if (!command.IsPrefixedBy(L"\"") ||
            !QString::fromWCharArray(command.Ptr()).endsWith("\"%1\""))
            return 1;
        added++;
        if (failure == 0)
            associated = true;
        return failure;
    }
    LONG Delete(unsigned, const UString &, const NRegistryAssoc::CShellExtInfo &) override {
        deleted++;
        if (failure == 0)
            associated = false;
        return failure;
    }
    void NotifyChanged() override { notifications++; }
};
} // namespace

class SystemPageTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void nativeUsernameAndPendingStateSurviveLanguageChanges() {
        QTemporaryDir fixture;
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->userName = QStringLiteral("пользователь 空");
        const QString settings = fixture.filePath("settings.ini");
        QtFmOptionsDialog dialog(settings, nullptr, manager, service);
        auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
        list->topLevelItem(1)->setSelected(true);
        dialog.findChild<QPushButton *>("systemCurrent")->click();
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        QCOMPARE(list->headerItem()->text(1), service->userName);
        QCOMPARE(list->headerItem()->text(2), QStringLiteral("Все пользователи"));
        QCOMPARE(list->topLevelItem(1)->text(1), QString("7-Zip"));
        dialog.reject();
        QCOMPARE(service->added, 0);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void sourcePageNormalCompactGeometryAndLanguage() {
        QTemporaryDir fixture;
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        const QString settings = fixture.filePath("settings.ini");
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), settings);
            QtFmOptionsDialog dialog(settings, nullptr, manager, service);
            QVERIFY(OptionsPresentation::show(dialog, "systemAssociations"));
            dialog.show();
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            for (const QSize size : {QSize(640, 390), QSize(520, 380)}) {
                dialog.resize(size);
                QApplication::processEvents();
                // Native styles can require a taller minimum than the requested
                // compact frame; preserve it while checking every control fits.
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("options-system-%1-%2").arg(language).arg(size.width())));
                for (const char *name :
                     {"systemCurrent", "systemAll", "systemAssociations", "settingsButtons"}) {
                    auto *control = dialog.findChild<QWidget *>(name);
                    OptionsPresentation::show(dialog, control);
                    QVERIFY2(UiInspection::fullyVisible(*control, dialog), name);
                    if (QString(name) != "systemAssociations")
                        QVERIFY2(control->width() >= control->sizeHint().width(), name);
                }
                auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
                for (int group = 0; group < 2; ++group) {
                    const auto buttonBounds = UiInspection::bounds(
                        *dialog.findChild<QWidget *>(group == 0 ? "systemCurrent" : "systemAll"),
                        dialog);
                    const int columnLeft =
                        list->header()
                            ->mapTo(&dialog,
                                    QPoint(list->header()->sectionViewportPosition(group + 1), 0))
                            .x();
                    QVERIFY(buttonBounds.center().x() >= columnLeft);
                    QVERIFY(buttonBounds.center().x() <
                            columnLeft + list->header()->sectionSize(group + 1));
                }
                QVERIFY(UiInspection::bounds(*dialog.findChild<QWidget *>("systemCurrent"), dialog)
                            .bottom() < UiInspection::bounds(*list, dialog).top());
                QVERIFY(UiInspection::separate(
                    *list, *dialog.findChild<QWidget *>("settingsButtons"), dialog));
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("options-system-%1-%2").arg(language).arg(size.width())));
            }
        }
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void actualOptionsApplyAndCancelUseInjectedService() {
        QTemporaryDir fixture;
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        {
            QtFmOptionsDialog dialog(fixture.filePath("settings.ini"), nullptr, manager, service);
            auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
            list->topLevelItem(1)->setSelected(true);
            dialog.findChild<QPushButton *>("systemCurrent")->click();
            QVERIFY(dialog.findChild<QDialogButtonBox *>("settingsButtons")
                        ->button(QDialogButtonBox::Apply)
                        ->isEnabled());
            dialog.reject();
        }
        QCOMPARE(service->added, 0);
        QtFmOptionsDialog dialog(fixture.filePath("settings.ini"), nullptr, manager, service);
        auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
        list->topLevelItem(1)->setSelected(true);
        dialog.findChild<QPushButton *>("systemCurrent")->click();
        dialog.apply();
        QCOMPARE(service->added, 1);
        QVERIFY(!dialog.findChild<QDialogButtonBox *>("settingsButtons")
                     ->button(QDialogButtonBox::Apply)
                     ->isEnabled());
        QCOMPARE(list->topLevelItem(1)->text(1), QString("7-Zip"));
    }
    void sourceAssociationFailureDoesNotVetoOptionsOk() {
        QTemporaryDir fixture;
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->failure = 5;
        QtFmOptionsDialog dialog(fixture.filePath("settings.ini"), nullptr, manager, service);
        dialog.show();
        auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
        list->topLevelItem(1)->setSelected(true);
        dialog.findChild<QPushButton *>("systemCurrent")->click();
        QTimer responder;
        widgetDialogs_.connectResponder(responder, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                message->accept();
                responder.stop();
            }
        });
        responder.start(10);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        QCOMPARE(list->topLevelItem(1)->text(1), QString());
        QCOMPARE(service->added, 1);
    }
    void sourceMixedSelectionCycleAndExplicitApply() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        CSystemPage page(manager, service);
        auto *list = page.findChild<QTreeWidget *>("systemAssociations");
        QCOMPARE(list->topLevelItemCount(), 3); // Original case-insensitive extension merge.
        QCOMPARE(list->topLevelItem(0)->text(1), QString("Other.Archiver"));
        auto *current = page.findChild<QPushButton *>("systemCurrent");
        current->click(); // No selection: the original mixed clear/other rule.
        QCOMPARE(list->topLevelItem(0)->text(1), QString("Other.Archiver"));
        QCOMPARE(list->topLevelItem(1)->text(1), QString("7-Zip"));
        QCOMPARE(service->added, 0);
        QVERIFY(page.IsModified());
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(service->added, 2);
        QCOMPARE(service->deleted, 0);
        QVERIFY(!page.IsModified());
        QVERIFY(page.WasChanged);
        list->clearSelection();
        list->topLevelItem(0)->setSelected(true);
        current->click();
        QCOMPARE(list->topLevelItem(0)->text(1), QString("7-Zip"));
        current->click();
        QCOMPARE(list->topLevelItem(0)->text(1), QString());
        current->click();
        QCOMPARE(list->topLevelItem(0)->text(1), QString("Other.Archiver"));
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(service->added, 2); // Returned to initial other state: no write.
        QCOMPARE(service->deleted, 0);
    }
    void nativeAliasCellsRefreshTogetherAfterApplyAndClear() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->nativeAliases = true;
        CSystemPage page(manager, service);
        auto *list = page.findChild<QTreeWidget *>("systemAssociations");
        auto *current = page.findChild<QPushButton *>("systemCurrent");
        list->topLevelItem(0)->setSelected(true);
        current->click();
        QCOMPARE(list->topLevelItem(1)->text(1), QString("Native.Other"));
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(service->added, 1);
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            QCOMPARE(list->topLevelItem(i)->text(1), QString("7-Zip"));
        current->click();
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(service->deleted, 1);
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            QCOMPARE(list->topLevelItem(i)->text(1), QString("Native.Other"));
    }
    void externalChangeRefreshesTheFailedRowBeforeAnotherDecision() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->ownIcon = SolidIcon(Qt::green);
        service->originalIcon = SolidIcon(Qt::red);
        service->externalIcon = SolidIcon(Qt::blue);
        CSystemPage page(manager, service);
        auto *list = page.findChild<QTreeWidget *>("systemAssociations");
        const auto original = list->findItems("zip", Qt::MatchExactly);
        const auto changed = list->findItems("7z", Qt::MatchExactly);
        QCOMPARE(original.size(), 1);
        QCOMPARE(changed.size(), 1);
        auto *row = changed.front();
        QCOMPARE(IconColor(*original.front()), QColor(Qt::red));
        row->setSelected(true);
        page.findChild<QPushButton *>("systemCurrent")->click();
        QCOMPARE(IconColor(*row), QColor(Qt::green));
        service->externallyChanged = true;
        service->failure = 1237;
        QCOMPARE(page.OnApply(), LONG(1237));
        QCOMPARE(row->text(1), QString("External.Archiver"));
        QCOMPARE(IconColor(*row), QColor(Qt::blue));
        QVERIFY(!page.IsModified());
        page.findChild<QPushButton *>("systemCurrent")->click();
        QCOMPARE(row->text(1), QString("7-Zip"));
        service->failure = 0;
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(row->text(1), QString("7-Zip"));
        QCOMPARE(IconColor(*row), QColor(Qt::green));
    }
    void sourceApplyFailureRevertsAndCancelDoesNotRegister() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->failure = 5;
        {
            CSystemPage page(manager, service);
            auto *list = page.findChild<QTreeWidget *>("systemAssociations");
            list->topLevelItem(1)->setSelected(true);
            page.findChild<QPushButton *>("systemAll")->click();
            QCOMPARE(page.OnApply(), LONG(5));
            QCOMPARE(list->topLevelItem(1)->text(2), QString());
            QVERIFY(!page.IsModified());
            page.findChild<QPushButton *>("systemCurrent")->click();
        }
        QCOMPARE(service->added, 1); // Destruction/Cancel has no apply side effect.
        QCOMPARE(service->notifications, 1);
    }
    void unavailableGroupsAndAbsentHandlerCannotChange() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->permitted[1] = false;
        CSystemPage page(manager, service);
        QVERIFY(!page.findChild<QPushButton *>("systemAll")->isEnabled());
        QVERIFY(!page.findChild<QPushButton *>("systemAll")->toolTip().isEmpty());
        auto *list = page.findChild<QTreeWidget *>("systemAssociations");
        QTest::keyClick(list, Qt::Key_Plus);
        QVERIFY(!page.IsModified());
        CSystemPage absent(nullptr, service);
        QVERIFY(!absent.findChild<QPushButton *>("systemCurrent")->isEnabled());
        QCOMPARE(absent.findChild<QTreeWidget *>("systemAssociations")->topLevelItemCount(), 0);
    }
    void unavailableFormatIsExplainedAndSkippedBySourceSelection() {
        CMyComPtr<IFolderManager> manager = new CFixtureManager;
        auto service = std::make_shared<CFakeAssociations>();
        service->unsupportedUnicodeFormat = true;
        CSystemPage page(manager, service);
        auto *list = page.findChild<QTreeWidget *>("systemAssociations");
        QVERIFY(!list->topLevelItem(2)->toolTip(1).isEmpty());
        list->topLevelItem(2)->setSelected(true);
        page.findChild<QPushButton *>("systemCurrent")->click();
        QVERIFY(!page.IsModified());
        QCOMPARE(service->added, 0);
        list->clearSelection();
        page.findChild<QPushButton *>("systemCurrent")->click();
        QCOMPARE(page.OnApply(), LONG(0));
        QCOMPARE(service->added, 1);
        QCOMPARE(list->topLevelItem(2)->text(1), QString());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SystemPageTests)
#include "system_page_tests.moc"
