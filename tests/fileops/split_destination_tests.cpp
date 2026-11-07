#include "archive/support/extract_dialog_fixture.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/extract/alias.h"
#include "fileops/patterns/Wildcard.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class SplitDestinationTests final : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QByteArray oldSettings_;
    QString settings_;
    QComboBox *path(CExtractDialog &d) { return d.findChild<QComboBox *>("extractDestination"); }
    QLineEdit *name(CExtractDialog &d) { return d.findChild<QLineEdit *>("extractPathName"); }
    QCheckBox *enabled(CExtractDialog &d) { return d.findChild<QCheckBox *>("extractNameEnable"); }
  private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = dir_.filePath("split-dest.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
    }
    void init() { QFile::remove(settings_); }
    void browsingUsesTheSameDestinationAsExtraction() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString start = fixture.filePath(QStringLiteral("папка с пробелом"));
        const QString selected = fixture.filePath(QStringLiteral("другая папка"));
        QVERIFY(QDir().mkpath(start));
        QVERIFY(QDir().mkpath(selected));
        const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard(
            [&] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
        CExtractDialog dialog(start, {});
        auto *browse = dialog.findChild<QPushButton *>("extractBrowseButton");
        QVERIFY(browse);
        const QString draft = "  " + start + "  ";
        path(dialog)->setEditText(draft);
        enabled(dialog)->setChecked(true);
        name(dialog)->setText(QStringLiteral("подпапка"));
        dialog.show();
        for (const bool accept : {false, true}) {
            QString openedDirectory;
            QTimer answer;
            connect(&answer, &QTimer::timeout, &dialog, [&] {
                auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (!picker)
                    return;
                answer.stop();
                openedDirectory = picker->directory().canonicalPath();
                const auto close = qScopeGuard([&] {
                    if (picker->isVisible())
                        picker->reject();
                });
                if (accept) {
                    picker->setDirectory(selected);
                    picker->selectFile(selected);
                    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
                }
            });
            answer.start(1);
            browse->click();
            QCOMPARE(openedDirectory, QFileInfo(start).canonicalFilePath());
            QCOMPARE(name(dialog)->text(), QStringLiteral("подпапка"));
            if (accept)
                QCOMPARE(QFileInfo(path(dialog)->currentText()).canonicalFilePath(),
                         QFileInfo(selected).canonicalFilePath());
            else
                QCOMPARE(path(dialog)->currentText(), draft);
        }
        QCOMPARE(QDir::fromNativeSeparators(dialog.destination()),
                 QDir(selected).filePath(QStringLiteral("подпапка")));
    }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void smartSplit_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("leaf");
        QTest::newRow("empty") << QString() << QString() << QString();
        QTest::newRow("root") << QString("/") << QString() << QString("/");
        QTest::newRow("relative") << QString("name") << QString() << QString("name");
        QTest::newRow("relative-slash") << QString("name/") << QString() << QString("name/");
        QTest::newRow("full") << QString("/a/b") << QString("/a/") << QString("b");
        QTest::newRow("trailing") << QString("/a/b/") << QString("/a/") << QString("b/");
        QTest::newRow("double-trailing") << QString("/a/b//") << QString("/a/b/") << QString("/");
        QTest::newRow("unicode") << QStringLiteral("/папка с пробелом/δ файл/")
                                 << QStringLiteral("/папка с пробелом/")
                                 << QStringLiteral("δ файл/");
#ifdef Q_OS_WIN
        QTest::newRow("native") << QString("C:\\folder\\name\\") << QString("C:\\folder\\")
                                << QString("name\\");
#else
        QTest::newRow("literal-backslash")
            << QString("folder\\name") << QString() << QString("folder\\name");
#endif
    }
    void smartSplit() {
        QFETCH(QString, input);
        QFETCH(QString, prefix);
        QFETCH(QString, leaf);
        QString actualPrefix, actualLeaf;
        SplitPathToParts_Smart(input, actualPrefix, actualLeaf);
        QCOMPARE(actualPrefix, prefix);
        QCOMPARE(actualLeaf, leaf);
    }
    void originalSplitControlsAndPersistence() {
        const QString initial = QStringLiteral("/папка с пробелом/вложенная/");
        {
            CExtractDialog dialog(initial, {});
            QVERIFY(enabled(dialog)->isChecked());
            QCOMPARE(path(dialog)->currentText(), QStringLiteral("/папка с пробелом/"));
            QCOMPARE(name(dialog)->text(), QStringLiteral("вложенная/"));
            QVERIFY(!name(dialog)->isHidden());
            name(dialog)->setText(QStringLiteral("  δ результат  "));
            QCOMPARE(dialog.destination(), QStringLiteral("/папка с пробелом/δ результат"));
            dialog.accept();
            QCOMPARE(QDir::fromNativeSeparators(dialog.destination()),
                     QStringLiteral("/папка с пробелом/δ результат/"));
        }
        NExtract::CInfo saved;
        saved.Load(settings_);
        QCOMPARE(QDir::fromNativeSeparators(saved.Paths[0]),
                 QStringLiteral("/папка с пробелом/δ результат/"));
        {
            CExtractDialog dialog(initial, {});
            enabled(dialog)->setChecked(false);
            QVERIFY(!name(dialog)->isHidden());
            QVERIFY(!name(dialog)->isEnabled());
            path(dialog)->setEditText(QStringLiteral("/другое место"));
            dialog.accept();
            QCOMPARE(QDir::fromNativeSeparators(dialog.destination()),
                     QStringLiteral("/другое место/"));
        }
        {
            CExtractDialog restarted(initial, {});
            QVERIFY(!enabled(restarted)->isChecked());
            QCOMPARE(path(restarted)->currentText(), initial);
            enabled(restarted)->setChecked(true);
            QVERIFY(!name(restarted)->isHidden());
            QVERIFY(name(restarted)->isEnabled());
            restarted.reject();
        }
        CExtractDialog afterCancel(initial, {});
        QVERIFY(!enabled(afterCancel)->isChecked());
    }
    void rootAndLeafDoNotDuplicate() {
        for (QString initial : {QString("/"), QString("name"), QString("name/")}) {
            QFile::remove(settings_);
            CExtractDialog dialog(initial, {});
            QCOMPARE(path(dialog)->currentText(), initial);
            QVERIFY(name(dialog)->text().isEmpty());
            dialog.accept();
            QCOMPARE(QDir::fromNativeSeparators(dialog.destination()),
                     initial.endsWith('/') ? initial : initial + "/");
        }
    }
    void extractionSuccessFailureAndCancel() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("source bytes"), qint64(12));
        file.close();
        ArchiveBackend backend;
        const QString archive = fixture.filePath("split-dest.zip");
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        const QString base = fixture.filePath(QStringLiteral("вывод"));
        const auto extract = [&](const QString &leaf, bool accept) {
            return ExtractDialogFixture::Run(
                backend, archive, base + "/initial", [&](CExtractDialog &dialog) {
                    enabled(dialog)->setChecked(true);
                    path(dialog)->setEditText(base);
                    name(dialog)->setText(leaf);
                    dialog.findChild<QCheckBox *>("extractElimDup")->setChecked(false);
                    if (accept)
                        dialog.accept();
                    else
                        dialog.reject();
                });
        };
        const auto success = extract(QStringLiteral("  δ результат  "), true);
        QCOMPARE(success.Result, S_OK);
        QVERIFY(success.CallbackOK);
        QFile restored(base + QStringLiteral("/δ результат/") + QFileInfo(source).fileName());
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArray("source bytes"));
        QFile occupied(base + "/occupied");
        QVERIFY(occupied.open(QIODevice::WriteOnly));
        QCOMPARE(occupied.write("preserve"), qint64(8));
        occupied.close();
        const QString corrected = base + "/corrected";
        const auto recovered = ExtractDialogFixture::Run(
            backend, archive, base + "/initial", [&](CExtractDialog &dialog) {
                const auto close = qScopeGuard([&] {
                    if (dialog.isVisible())
                        dialog.reject();
                });
                auto *error = dialog.findChild<QLabel *>("extractDestinationError");
                QVERIFY(error);
                enabled(dialog)->setChecked(true);
                name(dialog)->setText("occupied/child");
                const auto selectedMode = NExtract::NOverwriteMode::kSkip;
                auto *overwrite = dialog.findChild<QComboBox *>("extractOverwriteMode");
                overwrite->setCurrentIndex(overwrite->findData(selectedMode));
                path(dialog)->setEditText("  ");
                dialog.accept();
                QVERIFY(dialog.isVisible() && error->isVisible());
                QTRY_VERIFY(dialog.focusWidget() == path(dialog) ||
                            dialog.focusWidget() == path(dialog)->lineEdit());
                path(dialog)->setEditText(base);
                dialog.accept();
                QVERIFY(dialog.isVisible() && error->isVisible());
                QTRY_COMPARE(dialog.focusWidget(), name(dialog));
                QCOMPARE(overwrite->currentData().toInt(), int(selectedMode));
                QCOMPARE(name(dialog)->text(), QString("occupied/child"));
                name(dialog)->setText("corrected");
                QVERIFY(!error->isVisible());
                dialog.accept();
            });
        QCOMPARE(recovered.Result, S_OK);
        QVERIFY(recovered.CallbackOK);
        QFile retried(QDir(corrected).filePath(QFileInfo(source).fileName()));
        QVERIFY(retried.open(QIODevice::ReadOnly));
        QCOMPARE(retried.readAll(), QByteArray("source bytes"));
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("preserve"));
        const QString cancelledPath = base + "/cancelled";
        const auto cancelled = extract("cancelled", false);
        QCOMPARE(cancelled.Result, E_ABORT);
        QVERIFY(!QFileInfo::exists(cancelledPath));
    }
};
QTEST_MAIN(SplitDestinationTests)
#include "split_destination_tests.moc"
