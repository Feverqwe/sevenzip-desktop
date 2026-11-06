#include "archive/codecs/archivecodeccatalog.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/support/source_creation_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/compress/CompressDialog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/extract/alias.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

namespace {
QString normalized(const QString &path) { return QDir::fromNativeSeparators(path); }
void reports(QDialog &dialog, const QString &tag, bool extract) {
    if (extract) {
        auto *advanced = dialog.findChild<QToolButton *>("extractAdvancedToggle");
        auto *passwordEnabled = dialog.findChild<QCheckBox *>("extractPasswordEnabled");
        QVERIFY(advanced && passwordEnabled);
        advanced->setChecked(true);
        passwordEnabled->setChecked(true);
    }
    dialog.show();
    for (const QSize size :
         extract ? QList<QSize>{{700, 390}, {600, 340}} : QList<QSize>{{760, 720}, {640, 480}}) {
        dialog.resize(size);
        QApplication::processEvents();
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        if (extract) {
            for (const char *name : {"extractDestination", "extractPathMode",
                                     "extractOverwriteMode", "extractPassword", "extractElimDup"}) {
                auto *control = dialog.findChild<QWidget *>(name);
                QVERIFY(control);
                QVERIFY(UiInspection::fullyVisible(*control, dialog));
            }
            auto *leaf = dialog.findChild<QLineEdit *>("extractPathName");
            QVERIFY(leaf);
            if (leaf->isVisible())
                QVERIFY(UiInspection::fullyVisible(*leaf, dialog));
        } else {
            auto *name = dialog.findChild<QLineEdit *>("archiveName");
            QVERIFY(name);
            QVERIFY(UiInspection::fullyVisible(*name, dialog));
        }
        QVERIFY(UiInspection::writeReport(dialog, tag + '-' + QString::number(size.width())));
    }
}
} // namespace

class DialogPathRulesTests final : public QObject {
    Q_OBJECT
    QTemporaryDir _fixture;
    QByteArray _previousSettings;
    QString _settings;
  private slots:
    void initTestCase() {
        QVERIFY(_fixture.isValid());
        _settings = _fixture.filePath("settings.ini");
        _previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", _settings.toUtf8());
    }
    void init() { QFile::remove(_settings); }
    void cleanupTestCase() {
        if (_previousSettings.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", _previousSettings);
    }
    void nativePathRules() {
        CCompressDialog state;
        state.StartDirPrefix = _fixture.path();
        const QString name = QStringLiteral("\u00a0архив 空.zip\u00a0");
        state.m_ArchivePath = " \t" + name + "\t ";
        QString path;
        QVERIFY(state.GetFinalPath_Smart(path));
        QCOMPARE(normalized(path), _fixture.filePath(name));
        state.m_ArchivePath = QString("../").repeated(256) + "refused.zip";
        QVERIFY(!state.GetFinalPath_Smart(path));
        QVERIFY(path.isEmpty());
        state.DirPrefix = "unchanged";
        QString leaf = "unchanged";
        QVERIFY(!state.SetArcPathFields(state.m_ArchivePath, leaf, false));
        QCOMPARE(state.DirPrefix, QString("unchanged"));
        QCOMPARE(leaf, QString("unchanged"));
        QVERIFY(!state.SetArcPathFields(state.m_ArchivePath, leaf, true));
        QVERIFY(state.DirPrefix.isEmpty());
        QCOMPARE(leaf, state.m_ArchivePath);
    }
    void sharedHistoryComparison() {
        const QString kelvin = QString(QChar(0x212a));
        const bool same = UString(L"K").IsEqualTo_NoCase(UString(kelvin.toStdWString().c_str()));
        NCompression::CInfo saved;
        saved.ArcPaths.Add("K");
        saved.ArcPaths.Add(kelvin);
        saved.ArcPaths.Add("K");
        CCompressDialog state;
        state.StartDirPrefix = _fixture.path();
        state.m_ArchivePath = "new.zip";
        state.OnOK(saved);
        QCOMPARE(saved.ArcPaths.Size(), same ? 2u : 3u);
        QCOMPARE(saved.ArcPaths[1], QString("K"));
        CExtractDialog extract(_fixture.path(),
                               {"K", kelvin, "K", QStringLiteral("\u00a0old\u00a0")});
        extract.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
        extract.accept();
        NExtract::CInfo extracted;
        extracted.Load(_settings);
        QCOMPARE(extracted.Paths.Size(), same ? 3u : 4u);
        QCOMPARE(extracted.Paths.Back(), QStringLiteral("\u00a0old\u00a0"));
    }
    void realCreateExtract_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("split");
        for (const QString &language : {QString("-"), QString("ru")})
            for (bool split : {false, true})
                QTest::newRow(qPrintable(language + QString::number(split))) << language << split;
    }
    void realCreateExtract() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; real names workflow unverified");
#else
        QFETCH(QString, language);
        QFETCH(bool, split);
        SaveRegLang(UString(language.toStdWString().c_str()), _settings);
        ReloadLang(_settings);
        QTemporaryDir fixture;
        const QString input = fixture.filePath(QStringLiteral("данные 空.bin"));
        const QByteArray bytes("original\0bytes", 14);
        QVERIFY(SourceExtractFixture::writeFile(input, bytes));
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        const QString archiveName = QStringLiteral("\u00a0архив 空.zip\u00a0");
        const QString edited = " \t" + archiveName + "\t ";
        const QByteArray settingsBefore = SourceExtractFixture::readFile(_settings);
        {
            CreateArchiveDialog cancel(fixture.path(), {input}, nullptr, catalog);
            cancel.findChild<QLineEdit *>("archiveName")->setText(edited);
            reports(cancel, QString("dialog-path-create-cancel-%1-%2").arg(language).arg(split),
                    false);
            QTest::keyClick(&cancel, Qt::Key_Escape);
            QCOMPARE(cancel.result(), int(QDialog::Rejected));
            QCOMPARE(SourceExtractFixture::readFile(_settings), settingsBefore);
        }
        CreateArchiveDialog create(fixture.path(), {input}, nullptr, catalog);
        auto *format = create.findChild<QComboBox *>("archiveFormat");
        format->setCurrentIndex(format->findText("zip"));
        create.findChild<QLineEdit *>("archiveName")->setText(edited);
        reports(create, QString("dialog-path-create-%1-%2").arg(language).arg(split), false);
        create.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(create.result(), int(QDialog::Accepted));
        const QString archive = fixture.filePath(archiveName);
        QCOMPARE(normalized(create.target()), archive);
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, create.target(), {input}, {}, false,
                                               create.compressionOptions()),
                 S_OK);
        done.clear();
        QVERIFY(QFileInfo::exists(archive));
        const QByteArray archiveBytes = SourceExtractFixture::readFile(archive);
        backend.list(archive);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        done.clear();
        const QString base = fixture.filePath(QStringLiteral("\u00a0выход 空\u00a0"));
        const QString leaf = QStringLiteral("\u00a0папка δ\u00a0");
        const QString output = split ? base + '/' + leaf : base;
        const QByteArray savedBefore = SourceExtractFixture::readFile(_settings);
        const auto edit = [&](CExtractDialog &dialog) {
            dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(split);
            dialog.findChild<QComboBox *>("extractDestination")->setEditText(" \t" + base + "\t ");
            dialog.findChild<QLineEdit *>("extractPathName")->setText(" \t" + leaf + "\t ");
            dialog.findChild<QCheckBox *>("extractElimDup")->setChecked(false);
            QCOMPARE(normalized(dialog.destination()), output);
        };
        {
            CExtractDialog cancel(base, {});
            edit(cancel);
            reports(cancel, QString("dialog-path-extract-cancel-%1-%2").arg(language).arg(split),
                    true);
            QTest::keyClick(&cancel, Qt::Key_Escape);
            QCOMPARE(cancel.result(), int(QDialog::Rejected));
            QCOMPARE(SourceExtractFixture::readFile(_settings), savedBefore);
        }
        CExtractDialog extract(base, {});
        edit(extract);
        reports(extract, QString("dialog-path-extract-%1-%2").arg(language).arg(split), true);
        extract.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(normalized(extract.destination()), output + '/');
        ExtractionOptions options;
        options.ElimDup = extract.ElimDup;
        options.NtSecurity = extract.NtSecurity;
        backend.extract(archive, extract.destination(), {}, {}, options);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(QFileInfo(input).fileName())),
                 bytes);
        QCOMPARE(SourceExtractFixture::readFile(input), bytes);
        QCOMPARE(SourceExtractFixture::readFile(archive), archiveBytes);
        NCompression::CInfo compression;
        compression.Load(_settings);
        QCOMPARE(normalized(compression.ArcPaths[0]), archive);
        NExtract::CInfo extraction;
        extraction.Load(_settings);
        QCOMPARE(normalized(extraction.Paths[0]), output + '/');
        CExtractDialog reopened(base, {});
        auto *history = reopened.findChild<QComboBox *>("extractDestination");
        QVERIFY(history->findText(extraction.Paths[0]) >= 0);
#endif
    }
};
QTEST_MAIN(DialogPathRulesTests)
#include "dialog_path_rules_tests.moc"
