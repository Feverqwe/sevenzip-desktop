#include "Common/MyCom.h"
#include "Common/StringConvert.h"
#include "archive/support/source_extract_fixture.h"
#include "localization/LangUtils.h"
#include "platform/associations/qtassociationplatform.h"
#include "settings/SystemPage.h"
#include "settings/ViewSettings.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {
class FormatFixture final : public IFolderManager, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderManager)
  public:
    Z7_COM7F_IMF(OpenFolderFile(IInStream *, const wchar_t *, const wchar_t *, IFolderFolder **,
                                IProgress *))
    override { return E_NOTIMPL; }
    Z7_COM7F_IMF(GetExtensions(BSTR *extensions)) override {
        return StringToBstr(UString(L"zip 7z"), extensions);
    }
    Z7_COM7F_IMF(GetIconPath(const wchar_t *, BSTR *path, Int32 *index)) override {
        *index = 0;
        return StringToBstr(UString(), path);
    }
};
QTreeWidgetItem *row(QTreeWidget &list, const QString &extension) {
    const auto matches = list.findItems(extension, Qt::MatchExactly);
    return matches.size() == 1 ? matches.front() : nullptr;
}
QColor iconColor(const QTreeWidgetItem &item) {
    const QImage image = item.icon(0).pixmap(16, 16).toImage();
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}
} // namespace

class LinuxSystemPageTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void nativeIconsAndPendingCancel_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize &size : {QSize(640, 390), QSize(520, 380)})
                QTest::newRow(qPrintable(language + '-' + QString::number(size.width())))
                    << language << size;
    }
    void nativeIconsAndPendingCancel() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        const QString path = settings.filePath("options.ini");
        SaveRegLang(ToUString(language), path);
        ReloadLang(path);
        const auto platform = CreateNativeAssociationPlatform();
        const auto services = CreateQtAssociationServices(platform);
        QVERIFY(platform->CanApply(0));
        QVERIFY(!platform->CanApply(1));
        QtAssociationRecord beforeZip, before7z;
        QCOMPARE(platform->Read(0, UString(L"zip"), beforeZip), LONG(0));
        QCOMPARE(platform->Read(0, UString(L"7z"), before7z), LONG(0));
        QCOMPARE(ToQString(GetUnicodeString(beforeZip.info.ProgramKey)), QString("Red.desktop"));
        QCOMPARE(ToQString(GetUnicodeString(before7z.info.ProgramKey)), QString("Blue.desktop"));
        const QString preferences = qEnvironmentVariable("XDG_CONFIG_HOME") + "/mimeapps.list";
        const QByteArray original = SourceExtractFixture::readFile(preferences);
        CMyComPtr<IFolderManager> manager = new FormatFixture;
        QtFmOptionsDialog dialog(path, nullptr, manager, services);
        QVERIFY(OptionsPresentation::show(dialog, "associationChoices"));
        dialog.resize(size);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *list = dialog.findChild<QTreeWidget *>("systemAssociations");
        auto *page = dialog.findChild<CSystemPage *>("systemPage");
        auto *current = dialog.findChild<QPushButton *>("systemCurrent");
        auto *all = dialog.findChild<QPushButton *>("systemAll");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        auto *choices = dialog.findChild<QTreeWidget *>("associationChoices");
        auto *zipChoice = dialog.findChild<QCheckBox *>("association-zip");
        QVERIFY(list && page && current && all && buttons && choices && zipChoice);
        auto *zip = row(*list, "zip");
        auto *seven = row(*list, "7z");
        QVERIFY(zip && seven);
        const QString tag = "linux-system-" + QString::fromLatin1(QTest::currentDataTag());
        auto inspect = [&](const QString &state) {
            if (!OptionsPresentation::show(dialog, choices))
                return false;
            for (QWidget *widget : QList<QWidget *>{choices, zipChoice, buttons})
                if (!UiInspection::fullyVisible(*widget, dialog))
                    return false;
            dialog.setProperty("uiInspection_language", language == "ru" ? "ru" : "en");
            dialog.setProperty("uiInspection_state", state);
            return UiInspection::writeReport(dialog, tag + '-' + state);
        };
        QCOMPARE(zip->text(1), QString("Red.desktop"));
        QCOMPARE(seven->text(1), QString("Blue.desktop"));
        QCOMPARE(iconColor(*zip), QColor(220, 40, 30));
        QCOMPARE(iconColor(*seven), QColor(30, 70, 220));
        QVERIFY(current->isEnabled());
        QVERIFY(!all->isEnabled());
        QVERIFY(all->isHidden());
        QVERIFY(!all->toolTip().isEmpty());
        QVERIFY(zip->text(2).isEmpty());
        QCOMPARE(list->headerItem()->text(1), platform->CurrentUserName());
        QVERIFY(inspect("initial"));
        QVERIFY(!zipChoice->isChecked());
        zipChoice->click();
        QVERIFY(zipChoice->isChecked());
        QCOMPARE(zip->text(1), QString("7-Zip"));
        QCOMPARE(iconColor(*zip), QColor(30, 180, 70));
        QVERIFY(inspect("pending-own"));
        zipChoice->click();
        QVERIFY(!zipChoice->isChecked());
        QCOMPARE(zip->text(1), QString("Red.desktop"));
        QCOMPARE(iconColor(*zip), QColor(220, 40, 30));
        QCOMPARE(SourceExtractFixture::readFile(preferences), original);
        QtAssociationRecord stillZip;
        QCOMPARE(platform->Read(0, UString(L"zip"), stillZip), LONG(0));
        QCOMPARE(stillZip.revision, beforeZip.revision);
        zipChoice->click();
        buttons->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(zip->text(1), QString("7-Zip"));
        QCOMPARE(iconColor(*zip), QColor(30, 180, 70));
        QCOMPARE(seven->text(1), QString("Blue.desktop"));
        QCOMPARE(iconColor(*seven), QColor(30, 70, 220));
        QVERIFY(inspect("applied"));
        const QByteArray applied = SourceExtractFixture::readFile(preferences);
        QVERIFY(applied != original);
        zipChoice->click();
        QVERIFY(zip->text(1).isEmpty());
        QVERIFY(inspect("pending-clear"));
        dialog.reject();
        QCOMPARE(SourceExtractFixture::readFile(preferences), applied);
        QtAssociationRecord afterZip, after7z;
        QCOMPARE(platform->Read(0, UString(L"zip"), afterZip), LONG(0));
        QCOMPARE(ToQString(GetUnicodeString(afterZip.info.ProgramKey)), QString("7-Zip.desktop"));
        QCOMPARE(platform->Read(0, UString(L"7z"), after7z), LONG(0));
        QCOMPARE(after7z.revision, before7z.revision);
        QtFmOptionsDialog reopened(path, nullptr, manager, CreateQtAssociationServices(platform));
        QVERIFY(OptionsPresentation::show(reopened, "associationChoices"));
        reopened.resize(size);
        reopened.show();
        QVERIFY(QTest::qWaitForWindowActive(&reopened));
        auto *reopenedList = reopened.findChild<QTreeWidget *>("systemAssociations");
        QVERIFY(reopenedList);
        QCOMPARE(row(*reopenedList, "zip")->text(1), QString("7-Zip"));
        QCOMPARE(iconColor(*row(*reopenedList, "zip")), QColor(30, 180, 70));
        QCOMPARE(iconColor(*row(*reopenedList, "7z")), QColor(30, 70, 220));
        reopened.setProperty("uiInspection_language", language == "ru" ? "ru" : "en");
        reopened.setProperty("uiInspection_state", "reopened");
        QVERIFY(UiInspection::writeReport(reopened, tag + "-reopened"));
        auto *reopenedChoice = reopened.findChild<QCheckBox *>("association-zip");
        auto *reopenedPage = reopened.findChild<CSystemPage *>("systemPage");
        QVERIFY(reopenedChoice && reopenedPage && reopenedChoice->isChecked());
        reopenedChoice->click();
        QProcess external;
        external.start(QStandardPaths::findExecutable("gio"),
                       {"mime", "application/zip", "Red.desktop"});
        QVERIFY(external.waitForFinished(5000));
        QCOMPARE(external.exitCode(), 0);
        const QByteArray externalPreferences = SourceExtractFixture::readFile(preferences);
        QVERIFY(externalPreferences != applied);
        const auto currentHandler = [&] {
            QtAssociationRecord detected;
            return platform->Read(0, UString(L"zip"), detected) == 0
                       ? ToQString(GetUnicodeString(detected.info.ProgramKey))
                       : QString();
        };
        // Let GIO receive its file-monitor notification, as the real GUI loop does.
        QTRY_COMPARE(currentHandler(), QString("Red.desktop"));
        QSignalSpy errors(reopenedPage, &CSystemPage::ApplyError);
        QObject responseContext;
        bool messageSeen = false, messageValid = false, messageWritten = false;
        widgetDialogs_.respond(responseContext, [&](QDialog &active) {
            auto *message = qobject_cast<QMessageBox *>(&active);
            if (!message)
                return;
            messageSeen = true;
            messageValid = message->icon() == QMessageBox::Critical &&
                           message->standardButtons() == QMessageBox::Ok;
            message->setProperty("uiInspection_language", language == "ru" ? "ru" : "en");
            message->setProperty("uiInspection_state", "association-external-refusal");
            messageWritten = UiInspection::writeReport(*message, tag + "-external-message");
            message->done(QMessageBox::Ok);
        });
        QSignalSpy laterApplied(&reopened, &QtFmOptionsDialog::Applied);
        reopened.findChild<QDialogButtonBox *>("settingsButtons")
            ->button(QDialogButtonBox::Apply)
            ->click();
        QVERIFY(messageSeen && messageValid && messageWritten);
        QVERIFY(!laterApplied.isEmpty() && reopened.isVisible());
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.front().front().toLongLong(), qint64(1237)); // ERROR_RETRY.
        QCOMPARE(row(*reopenedList, "zip")->text(1), QString("Red.desktop"));
        QCOMPARE(iconColor(*row(*reopenedList, "zip")), QColor(220, 40, 30));
        QCOMPARE(iconColor(*row(*reopenedList, "7z")), QColor(30, 70, 220));
        QCOMPARE(SourceExtractFixture::readFile(preferences), externalPreferences);
        reopened.setProperty("uiInspection_state", "external-refusal");
        QVERIFY(UiInspection::writeReport(reopened, tag + "-external-refusal"));
        reopened.reject();
    }
};

int main(int argc, char **argv) {
    // Each CTest case has its own process: GIO caches XDG locations and handlers.
    QTemporaryDir fixture;
    if (!fixture.isValid())
        return 2;
    for (const QString &directory :
         {"config", "data/applications", "system", "icons/theme/16x16/apps", "bin"})
        if (!QDir().mkpath(fixture.filePath(directory)))
            return 2;
    qputenv("XDG_CONFIG_HOME", fixture.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixture.filePath("data").toUtf8());
    qputenv("XDG_CONFIG_DIRS", fixture.filePath("system").toUtf8());
    qputenv("XDG_DATA_DIRS", fixture.filePath("system").toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "");
    QApplication application(argc, argv);
    for (const auto &entry :
         {qMakePair(QString::fromUtf8("red 空 image.png"), QColor(220, 40, 30)),
          qMakePair(QString("icons/theme/16x16/apps/fixture-blue.png"), QColor(30, 70, 220)),
          qMakePair(QString::fromUtf8("own 空.png"), QColor(30, 180, 70))}) {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(entry.second);
        if (!image.save(fixture.filePath(entry.first)))
            return 2;
    }
    QIcon::setThemeSearchPaths({fixture.filePath("icons")});
    QIcon::setThemeName("theme");
    QString executable = fixture.filePath(QString::fromUtf8("bin/Sevenzip 空 test"));
    if (!QFile::link(QCoreApplication::applicationFilePath(), executable))
        return 2;
    executable.replace('\\', "\\\\").replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$");
    executable.replace('%', "%%");
    const auto desktop = [&](const QString &name, const QString &icon) {
        return SourceExtractFixture::writeFile(
            fixture.filePath("data/applications/" + name + ".desktop"),
            "[Desktop Entry]\nType=Application\nName=" + name.toUtf8() + "\nExec=\"" +
                executable.toUtf8() + "\" %F\nIcon=" + icon.toUtf8() + "\n");
    };
    if (!desktop("Red", fixture.filePath(QString::fromUtf8("red 空 image.png"))) ||
        !desktop("Blue", "fixture-blue") ||
        !desktop("sevenzip_desktop", fixture.filePath(QString::fromUtf8("own 空.png"))) ||
        !SourceExtractFixture::writeFile(fixture.filePath("icons/theme/index.theme"),
                                         "[Icon "
                                         "Theme]\nName=Fixture\nDirectories=16x16/apps\n[16x16/"
                                         "apps]\nSize=16\nType=Fixed\nContext=Applications\n") ||
        !SourceExtractFixture::writeFile(fixture.filePath("config/mimeapps.list"),
                                         "[Default "
                                         "Applications]\napplication/zip=Red.desktop;\napplication/"
                                         "x-7z-compressed=Blue.desktop;\n"))
        return 2;
    LinuxSystemPageTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "linux_system_page_tests.moc"
