#include "dialogs/browse/BrowseDialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "support/widget_dialogs.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class BrowseDialogTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void sourceCommonDialogFlags_data() {
        QTest::addColumn<bool>("save");
        QTest::addColumn<bool>("noFilters");
        QTest::newRow("open-new-name") << false << false;
        QTest::newRow("save-existing-without-extra-prompt") << true << false;
        QTest::newRow("open-without-filter") << false << true;
    }
    void sourceCommonDialogFlags() {
        QFETCH(bool, save);
        QFETCH(bool, noFilters);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString selected = fixture.filePath(QStringLiteral("new 密碼 with spaces.7z"));
        if (save) {
            QFile existing(selected);
            QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            existing.write("original bytes");
        }
        CBrowseInfo browse;
        browse.FilePath = ToUString(selected);
        browse.SaveMode = save;
        QCOMPARE(browse.FilterIndex, -1);
        CObjectVector<CBrowseFilterInfo> filters;
        if (!noFilters) {
            browse.FilterIndex = 0;
            filters.AddNew().Description = "Archive";
            filters[0].Masks.Add(UString("*.7z"));
        }
        QObject timerContext;
        bool shown = false;
        bool unexpectedPrompt = false;
        const QString currentDirectory = QDir::currentPath();
        widgetDialogs_.respond(timerContext, [&](QDialog &active) {
            auto *dialog = qobject_cast<QFileDialog *>(&active);
            if (!dialog)
                return;
            shown = true;
            dialog->setFocus();
            dialog->selectFile(selected);
            widgetDialogs_.respond(timerContext, [&](QDialog &activePrompt) {
                if (auto *prompt = qobject_cast<QMessageBox *>(&activePrompt)) {
                    unexpectedPrompt = true;
                    prompt->done(QMessageBox::No);
                }
            });
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            if (dialog->isVisible())
                dialog->reject();
        });
        const bool accepted = browse.BrowseForFile(filters);
        QVERIFY(shown);
        QVERIFY(!unexpectedPrompt);
        QVERIFY(accepted);
        QCOMPARE(QDir::fromNativeSeparators(ToQString(browse.FilePath)), selected);
        QCOMPARE(QDir::currentPath(), currentDirectory);
        QCOMPARE(browse.FilterIndex, noFilters ? -1 : 0);
        if (save) {
            QFile existing(selected);
            QVERIFY(existing.open(QIODevice::ReadOnly));
            QCOMPARE(existing.readAll(), QByteArray("original bytes"));
        } else
            QVERIFY(!QFile::exists(selected));
    }

    void folderCancelAndUnicodeSelection() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString selected = fixture.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QDir().mkdir(selected));
        UString result = L"stale";
        widgetDialogs_.respond(*this, [](QDialog &active) {
            if (auto *dialog = qobject_cast<QFileDialog *>(&active))
                dialog->reject();
        });
        QVERIFY(!MyBrowseForFolder(nullptr, L"Папка", ToUString(fixture.path()), result));
        QVERIFY(result.IsEmpty());
        widgetDialogs_.respond(*this, [selected](QDialog &active) {
            if (auto *dialog = qobject_cast<QFileDialog *>(&active)) {
                dialog->setDirectory(selected);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        QVERIFY(MyBrowseForFolder(nullptr, L"Папка", ToUString(fixture.path()), result));
        QCOMPARE(ToQString(result), selected);
    }

    void firstArchiveVolumeUsesArchiveFilter_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("archives");
        QTest::addColumn<QString>("allFiles");
        QTest::newRow("English") << QString("-") << QString("Archives") << QString("All files");
        QTest::newRow("Russian") << QString("ru") << QStringLiteral("Архивы")
                                 << QStringLiteral("Все файлы");
    }
    void firstArchiveVolumeUsesArchiveFilter() {
        QFETCH(QString, language);
        QFETCH(QString, archives);
        QFETCH(QString, allFiles);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        const QString selected = fixture.filePath(QStringLiteral("архив с пробелом.7z.001"));
        QFile file(selected);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CBrowseInfo browse;
        browse.lpstrTitle = L"Открыть архив";
        CObjectVector<CBrowseFilterInfo> filters;
        filters.AddNew().Description = ToUString(archives);
        filters[0].Masks.Add(UString("*.7z"));
        filters[0].Masks.Add(UString("*.zip"));
        filters[0].Masks.Add(UString("*.001"));
        filters.AddNew().Description = ToUString(allFiles);
        filters[1].Masks.Add(UString("*"));
        browse.FilterIndex = 0;
        browse.FilePath = ToUString(fixture.path() + '/');
        bool answered = false;
        QStringList observedFilters;
        QString selectedFilter;
        widgetDialogs_.respond(*this, [&](QDialog &active) {
            if (auto *dialog = qobject_cast<QFileDialog *>(&active)) {
                answered = true;
                selectedFilter = dialog->selectedNameFilter();
                observedFilters = dialog->nameFilters();
                dialog->setFocus();
                dialog->selectFile(selected);
                QCOMPARE(dialog->selectedFiles(), QStringList{selected});
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        QVERIFY(browse.BrowseForFile(filters));
        QVERIFY(answered);
        QVERIFY(selectedFilter.contains("*.001"));
        QCOMPARE(observedFilters.size(), 2);
        QVERIFY(observedFilters.at(0).startsWith(archives + " ("));
        QCOMPARE(observedFilters.at(1), allFiles + " (*)");
        QCOMPARE(ToQString(browse.FilePath), selected);
        QCOMPARE(browse.FilterIndex, 0);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
    }
    void fileCancelRestoresPathThenSelectsUnicodeFile() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString selected = fixture.filePath(QStringLiteral("архив с пробелом.7z"));
        QFile file(selected);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CBrowseInfo browse;
        browse.lpstrTitle = L"Открыть архив";
        CObjectVector<CBrowseFilterInfo> filters;
        filters.AddNew().Description = L"Архивы";
        filters[0].Masks.Add(UString("*.7z"));
        filters.AddNew().Description = L"Все файлы";
        filters[1].Masks.Add(UString("*"));
        browse.FilePath = ToUString(selected);
        browse.FilterIndex = 1;
        const QString cancelled = fixture.filePath(QStringLiteral("отменённый выбор.7z"));
        bool edited = false;
        widgetDialogs_.respond(*this, [&](QDialog &active) {
            if (auto *dialog = qobject_cast<QFileDialog *>(&active)) {
                dialog->setFocus();
                dialog->selectNameFilter(dialog->nameFilters().front());
                dialog->selectFile(cancelled);
                edited = dialog->selectedFiles() == QStringList{cancelled} &&
                         dialog->selectedNameFilter() == dialog->nameFilters().front();
                dialog->reject();
            }
        });
        QVERIFY(!browse.BrowseForFile(filters));
        QVERIFY(edited);
        QCOMPARE(ToQString(browse.FilePath), selected);
        QCOMPARE(browse.FilterIndex, 1);
        widgetDialogs_.respond(*this, [selected](QDialog &active) {
            if (auto *dialog = qobject_cast<QFileDialog *>(&active)) {
                dialog->setFocus();
                dialog->selectFile(selected);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        QVERIFY(browse.BrowseForFile(filters));
        QCOMPARE(ToQString(browse.FilePath), selected);
        QCOMPARE(browse.FilterIndex, 1);
    }
};

QTEST_MAIN(BrowseDialogTests)
#include "browse_dialog_tests.moc"
