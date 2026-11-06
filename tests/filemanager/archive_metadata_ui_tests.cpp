#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "platform/support/native_link_fixture.h"
#include "support/archive_expert.h"
#include <QScopeGuard>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class ArchiveMetadataUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int rowForName(MainWindow &window, const QString &name) {
        for (int row = 0; row < table(window)->rowCount(); ++row)
            if (table(window)->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name)
                return row;
        return -1;
    }
  private slots:
    void metadataAddAndSelectedExtract_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("cancel");
        for (const QString &language : {QString("-"), QString("ru")}) {
            QTest::newRow(qPrintable(language + "-success")) << language << false;
            QTest::newRow(qPrintable(language + "-cancel")) << language << true;
        }
    }
    void metadataAddAndSelectedExtract() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module required.");
#else
        QFETCH(QString, language);
        QFETCH(bool, cancel);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString rootName = QStringLiteral("папка с пробелом δ");
        const QString root = fixture.filePath(rootName);
        QVERIFY(QDir().mkpath(root));
        QVERIFY(write(root + "/payload.bin", "metadata bytes"));
        QVERIFY(CreateNativeSymbolicLink(root + "/file link", "payload.bin"));
        QVERIFY(CreateNativeSymbolicLink(root + "/dangling link", "missing"));
        QVERIFY(write(fixture.filePath("keep.txt"), "original"));
        const QString archive = fixture.filePath("metadata.tar");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(QStandardPaths::findExecutable("7z"),
                      {"a", "-ttar", "--", archive, "keep.txt"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray original = read(archive);
        ArchiveBackend backend;
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE)));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        MainWindow window(&backend);
        window.show();
        CreateArchiveDialog create(fixture.path(), {root}, &window, backend.codecCatalog());
        bool optionsSeen = false, formAnswered = false;
        QStringList errors;
        QTimer dismissErrors;
        connect(&dismissErrors, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errors.append(box->text());
                    box->accept();
                }
        });
        dismissErrors.start(1);
        QTimer::singleShot(0, &window, [&] {
            auto *form = &create;
            QVERIFY(form);
            const auto close = qScopeGuard([&] {
                if (form->isVisible())
                    form->reject();
            });
            QCOMPARE(form->sources(), QStringList{root});
            form->findChild<QComboBox *>("archiveFormat")->setCurrentText("tar");
            form->findChild<QLineEdit *>("archiveName")->setText(archive);
            QVERIFY(EditArchiveMetadata(*form, true, [&](QDialog &editor) {
                auto *options = &editor;
                auto *sym = options->findChild<QCheckBox *>("IDX_COMPRESS_NT_SYM_LINKS");
                QVERIFY(sym && sym->isVisible() && sym->isEnabled());
                sym->setChecked(true);
                optionsSeen = true;
            }));
            QVERIFY(optionsSeen && form->compressionOptions().SymLinks.Val);
            form->findChild<QCheckBox *>("archiveDeleteAfterCompression")->setChecked(true);
            formAnswered = true;
            if (cancel)
                form->reject();
            else
                form->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        create.exec();
        QVERIFY(optionsSeen && formAnswered);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        if (cancel) {
            QCOMPARE(read(archive), original);
            QCOMPARE(read(root + "/payload.bin"), QByteArray("metadata bytes"));
            QVERIFY(QFileInfo(root + "/dangling link").isSymLink());
            return;
        }
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {root}, {}, false,
                                               create.compressionOptions()),
                 S_OK);
        QVERIFY(!QFileInfo::exists(root));
        window.openArchive(archive);
        QTRY_VERIFY(rowForName(window, rootName) >= 0);
        table(window)->selectRow(rowForName(window, rootName));
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        const QString output = fixture.filePath("selected output");
        window.extractTo(output, true, {}, options);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(read(output + "/" + rootName + "/payload.bin"), QByteArray("metadata bytes"));
        QVERIFY(QFileInfo(output + "/" + rootName + "/file link").isSymLink());
        QVERIFY(QFileInfo(output + "/" + rootName + "/dangling link").isSymLink());
        QVERIFY(!QFileInfo::exists(output + "/keep.txt"));
        openRow(window, rowForName(window, rootName));
        QTRY_VERIFY(rowForName(window, "payload.bin") >= 0);
        options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
        options.currentFolder = rootName;
        const QString currentOutput = fixture.filePath("current folder output");
        window.extractTo(currentOutput, false, {}, options);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(read(currentOutput + "/payload.bin"), QByteArray("metadata bytes"));
        QVERIFY(QFileInfo(currentOutput + "/file link").isSymLink());
        QVERIFY(QFileInfo(currentOutput + "/dangling link").isSymLink());
        QVERIFY(!QFileInfo::exists(currentOutput + "/" + rootName));
        QVERIFY(!backend.busy());
#endif
    }
};
QTEST_MAIN(ArchiveMetadataUiTests)
#include "archive_metadata_ui_tests.moc"
