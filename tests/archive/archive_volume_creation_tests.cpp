#include "archive/backend/archivebackend.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

namespace {
QString engine() { return QStandardPaths::findExecutable("7z"); }
bool writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool runEngine(const QStringList &args) {
    QProcess process;
    process.start(engine(), args);
    return process.waitForFinished(30000) && process.exitCode() == 0 &&
           process.exitStatus() == QProcess::NormalExit;
}
} // namespace

class ArchiveVolumeCreationTests final : public QObject {
    Q_OBJECT
    QTemporaryDir preferences_;
    QByteArray previousSettings_;
    QString settings_;
  private slots:
    void initTestCase() {
        QVERIFY(!engine().isEmpty());
        QVERIFY(preferences_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = preferences_.filePath("options.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
    }
    void init() {
        QFile::remove(settings_);
        SaveRegLang(UString("-"), settings_);
        ReloadLang(settings_);
    }
    void cleanup() {
        SaveRegLang(UString("-"), settings_);
        ReloadLang(settings_);
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void createVariableVolumes_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("protectedArchive");
        QTest::addColumn<bool>("russian");
        for (bool russian : {false, true})
            for (const QString &format : {QString("zip"), QString("7z")}) {
                const QString suffix = russian ? "-ru" : "-en";
                QTest::newRow(qPrintable(format + suffix)) << format << false << russian;
                QTest::newRow(qPrintable(format + "-protected" + suffix))
                    << format << true << russian;
            }
    }
    void createVariableVolumes() {
        QFETCH(QString, format);
        QFETCH(bool, protectedArchive);
        QFETCH(bool, russian);
        SaveRegLang(UString(russian ? "ru" : "-"), settings_);
        ReloadLang(settings_);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("source-a/first.bin");
        const QString second = fixture.filePath(QStringLiteral("source-b/имя с пробелом.bin"));
        const QByteArray a(240000, 'a'), b(200000, 'b');
        QVERIFY(writeFile(first, a));
        QVERIFY(writeFile(second, b));
        const QString target = fixture.filePath(QStringLiteral("архив с пробелом.") + format);
        CompressionOptions options;
        options.Level = 0;
        options.VolumeSizes.Add(64 << 10);
        options.VolumeSizes.Add(128 << 10);
        if (format == "zip" && protectedArchive)
            options.EncryptionMethod = "AES256";
        const QString password = protectedArchive ? QStringLiteral("fixture password") : QString();
        ArchiveBackend backend(nullptr, engine());
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {first, second}, password,
                                               format == "7z", options),
                 S_OK);
        QVERIFY(!backend.busy());
        QVERIFY(!QFileInfo::exists(target));
        const QStringList volumes =
            fixture.path().isEmpty()
                ? QStringList()
                : QDir(fixture.path())
                      .entryList({QFileInfo(target).fileName() + ".*"}, QDir::Files, QDir::Name);
        QVERIFY(volumes.size() >= 4);
        for (int i = 0; i < volumes.size(); ++i) {
            QCOMPARE(volumes[i], QFileInfo(target).fileName() + '.' +
                                     QString::number(i + 1).rightJustified(3, '0'));
            const qint64 size = QFileInfo(fixture.filePath(volumes[i])).size();
            QVERIFY(size > 0);
            if (i + 1 != volumes.size())
                QCOMPARE(size, qint64(i == 0 ? 64 << 10 : 128 << 10));
            else
                QVERIFY(size <= 128 << 10);
        }
        QStringList passwordArgs;
        if (protectedArchive)
            passwordArgs << "-p" + password;
        if (protectedArchive) {
            QVERIFY(!runEngine({"t", "-y", "-pwrong", "--", target + ".001"}));
            QProcess listing;
            listing.start(engine(), QStringList{"l", "-slt"} + passwordArgs +
                                        QStringList{"--", target + ".001"});
            QVERIFY(listing.waitForFinished(30000));
            QCOMPARE(listing.exitCode(), 0);
            QCOMPARE(listing.readAllStandardOutput().count("Encrypted = +"), 2);
        }
        QVERIFY(
            runEngine(QStringList{"t", "-y"} + passwordArgs + QStringList{"--", target + ".001"}));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(QStringList{"x", "-y", "-o" + extracted} + passwordArgs +
                          QStringList{"--", target + ".001"}));
        QCOMPARE(readFile(QDir(extracted).filePath(QFileInfo(first).fileName())), a);
        QCOMPARE(readFile(QDir(extracted).filePath(QFileInfo(second).fileName())), b);
        QCOMPARE(readFile(first), a);
        QCOMPARE(readFile(second), b);
        QVERIFY(
            QDir(fixture.path())
                .entryList({".sevenzip-*"}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
    }
    void volumeDialogValidationAndGeometry_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("normal") << 760 << 720;
        QTest::newRow("compact") << 640 << 480;
    }
    void occupiedFirstVolumeCanBeCorrected() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.bin");
        const QString occupied = fixture.filePath("archive.7z.001");
        QVERIFY(writeFile(source, "source"));
        QVERIFY(writeFile(occupied, "keep this volume"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        name->setText("archive.7z");
        dialog.findChild<QComboBox *>("archiveVolumeSize")->setEditText("128K");
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        dialog.show();
        const auto submitAndDismissWarning = [&] {
            bool refused = false;
            QTimer::singleShot(0, &dialog, [&] {
                if (auto *warning =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    refused = true;
                    warning->accept();
                }
            });
            ok->click();
            return refused;
        };
        QVERIFY(submitAndDismissWarning() && dialog.isVisible());
        QCOMPARE(readFile(occupied), QByteArray("keep this volume"));
        const QString baseArchive = fixture.filePath("existing.7z");
        QVERIFY(writeFile(baseArchive, "keep this archive"));
        name->setText("existing.7z");
        QVERIFY(submitAndDismissWarning() && dialog.isVisible());
        QCOMPARE(readFile(baseArchive), QByteArray("keep this archive"));
        name->setText("different.7z");
        ok->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                               false, dialog.compressionOptions()),
                 S_OK);
        QVERIFY(QFileInfo(fixture.filePath("different.7z.001")).isFile());
    }
    void smallVolumeConfirmationDoesNotSaveUntilYes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.bin");
        QVERIFY(writeFile(source, "source"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        volume->setEditText("64K");
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        const QByteArray original = readFile(settings_);
        dialog.show();
        for (const auto answer : {QMessageBox::No, QMessageBox::Cancel, QMessageBox::Yes}) {
            ok->setFocus();
            QTimer::singleShot(0, &dialog, [answer] {
                auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(message);
                const auto close = qScopeGuard([&] {
                    if (message->isVisible())
                        message->reject();
                });
                QVERIFY(message->text().contains(QLocale().toString(65536)));
                message->button(answer)->click();
            });
            ok->click();
            if (answer != QMessageBox::Yes) {
                QCOMPARE(dialog.result(), int(QDialog::Rejected));
                QVERIFY(volume->isVisible());
                QTRY_VERIFY(dialog.focusWidget() == volume ||
                            dialog.focusWidget() == volume->lineEdit());
                QCOMPARE(volume->currentText(), QString("64K"));
                QCOMPARE(readFile(settings_), original);
            }
        }
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.compressionOptions().VolumeSizes[0], UInt64(65536));
    }
    void volumeDialogValidationAndGeometry() {
        QFETCH(int, width);
        QFETCH(int, height);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.bin");
        QVERIFY(writeFile(source, "source"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        QVERIFY(volume && volume->isEditable() && volume->isEnabled());
        QCOMPARE(volume->currentText(), QString());
        volume->setEditText("64K 128K");
        dialog.resize(width, height);
        dialog.show();
        ExpandArchiveSections(dialog);
        QCoreApplication::processEvents();
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        scroll->ensureWidgetVisible(volume);
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*volume, dialog));
        auto *footer = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(UiInspection::fullyVisible(*footer, dialog));
        QVERIFY(UiInspection::separate(*volume, *footer, dialog));
        QVERIFY(UiInspection::writeReport(dialog, "archive-volumes-" + QString::number(width)));
        const QByteArray original = readFile(settings_);
        auto *section = dialog.findChild<QtDisclosure *>("archiveAdditionalSection");
        for (const auto &invalid : {QString("0"), QString("128K") + QChar::Null + " 256K"}) {
            volume->setEditText(invalid);
            section->setExpanded(false);
            bool reported = false;
            QTimer::singleShot(0, &dialog, [&] {
                if (auto *message =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    reported = true;
                    message->accept();
                }
            });
            footer->button(QDialogButtonBox::Ok)->click();
            QVERIFY(reported);
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QCOMPARE(readFile(settings_), original);
            QVERIFY(section->isExpanded());
        }
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*volume, dialog));
        volume->setEditText("64K 128K");
        footer->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto sizes = dialog.compressionOptions().VolumeSizes;
        QCOMPARE(sizes.Size(), 2u);
        QCOMPARE(sizes[0], UInt64(64 << 10));
        QCOMPARE(sizes[1], UInt64(128 << 10));
        CreateArchiveDialog update(fixture.path(), {source});
        update.findChild<QLineEdit *>("archiveName")->setText(fixture.filePath("existing.zip"));
        QVERIFY(update.findChild<QComboBox *>("archiveVolumeSize")->isEnabled());
    }
};
QTEST_MAIN(ArchiveVolumeCreationTests)
#include "archive_volume_creation_tests.moc"
