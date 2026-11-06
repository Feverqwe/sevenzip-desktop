#include "archive/support/native_archive_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "gui/UpdateGUI.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/archive_expert.h"
#include "ui/support/layout_inspection.h"
#include <QFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray bytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
auto properties(const NCompressDialog::CInfo &info) {
    QList<QPair<QString, QString>> values;
    const auto native = NCompressDialog::GetCompressionProperties(info, true);
    FOR_VECTOR(i, native)
    values.append({QString::fromWCharArray(native[i].Name.Ptr()),
                   QString::fromWCharArray(native[i].Value.Ptr())});
    return values;
}
QComboBox *combo(QWidget &view, const char *name) { return view.findChild<QComboBox *>(name); }
QLineEdit *text(QWidget &view, const char *name) { return view.findChild<QLineEdit *>(name); }
} // namespace

class CompressionExpertTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    QByteArray oldSettings_;
    QString settings_;

  private slots:
    void initTestCase() {
        QVERIFY(fixture_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = fixture_.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
        const auto catalog = NativeArchiveFixture::Catalog();
        QVERIFY(catalog && catalog->result() == S_OK);
    }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void init() { QFile::remove(settings_); }
    void nestedCancelRestoresAppliedDraft_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("automatic");
        for (const QString &language : {QString("-"), QString("ru")})
            for (bool automatic : {false, true})
                QTest::newRow(qPrintable(language + (automatic ? "-auto" : "-explicit")))
                    << language << automatic;
    }
    void nestedCancelRestoresAppliedDraft() {
        QFETCH(QString, language);
        QFETCH(bool, automatic);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(L"-", settings_);
            ReloadLang(settings_);
        });
        const QByteArray saved = bytes(settings_);
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        form.show();
        const QString raw = QStringLiteral("  tm=off  ");
        QVERIFY(EditArchiveExpert(form, true, [&](QDialog &editor) {
            auto *method = combo(editor, "compressionMethod");
            method->setCurrentIndex(method->findData(automatic ? -1 : NCompressDialog::kLZMA2));
            for (const char *name :
                 {"compressionDictionary", "compressionWordSize", "compressionSolidBlock",
                  "compressionMemoryLimit", "compressionThreads"}) {
                auto *field = combo(editor, name);
                QVERIFY(field->count() > 1);
                field->setCurrentIndex(automatic ? 0 : 1);
            }
            text(editor, "archiveParameters")->setText(raw);
        }));
        QVERIFY(!QTest::currentTestFailed());
        const auto original = properties(form.sourceInfo());
        const auto method = combo(form, "compressionMethod")->currentData();
        const auto dictionary = combo(form, "compressionDictionary")->currentData();
        const auto threads = combo(form, "compressionThreads")->currentData();
        const auto solid = combo(form, "compressionSolidBlock")->currentData();
        const auto memory = combo(form, "compressionMemoryLimit")->currentData();
        text(form, "archiveName")->setText(QStringLiteral("своё имя 空.7z"));
        const QString name = text(form, "archiveName")->text();
        QVERIFY(EditArchiveExpert(form, false, [&](QDialog &editor) {
            combo(editor, "compressionMethod")->setCurrentText("PPMd");
            combo(editor, "compressionDictionary")
                ->setCurrentIndex(combo(editor, "compressionDictionary")->count() - 1);
            text(editor, "archiveParameters")->setText("tm=on");
        }));
        QCOMPARE(properties(form.sourceInfo()), original);
        QCOMPARE(combo(form, "compressionMethod")->currentData(), method);
        QCOMPARE(combo(form, "compressionDictionary")->currentData(), dictionary);
        QCOMPARE(combo(form, "compressionThreads")->currentData(), threads);
        QCOMPARE(combo(form, "compressionSolidBlock")->currentData(), solid);
        QCOMPARE(combo(form, "compressionMemoryLimit")->currentData(), memory);
        QCOMPARE(text(form, "archiveParameters")->text(), raw);
        QCOMPARE(text(form, "archiveName")->text(), name);
        QCOMPARE(bytes(settings_), saved);
        form.reject();
        QCOMPARE(bytes(settings_), saved);
    }
    void appliedOptionsReachConfirmedRequest() {
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        form.show();
        QVERIFY(EditArchiveExpert(form, true, [&](QDialog &editor) {
            combo(editor, "compressionMethod")->setCurrentText("LZMA2");
            combo(editor, "compressionDictionary")->setCurrentIndex(1);
            text(editor, "archiveParameters")->setText("tm=off");
        }));
        const auto selected = properties(form.sourceInfo());
        QVERIFY(!QFile::exists(settings_));
        form.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(form.result(), int(QDialog::Accepted));
        QCOMPARE(properties(form.sourceInfo()), selected);
        QVERIFY(!bytes(settings_).isEmpty());
    }
    void inlineMetadataCanBeCancelled_data() {
        QTest::addColumn<bool>("sourceBound");
        QTest::newRow("standalone") << false;
        QTest::newRow("source-bound") << true;
    }
    void inlineMetadataCanBeCancelled() {
        QFETCH(bool, sourceBound);
        const auto catalog = NativeArchiveFixture::Catalog();
        const QString input = fixture_.filePath("selected.txt");
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("unchanged");
        file.close();
        CCompressDialog source;
        source.Info.FormatIndex = catalog->formatIndex("tar");
        source.Info.ArcPath = fixture_.filePath("output.tar").toStdWString().c_str();
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(UString(input.toStdWString().c_str()));
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog->nativeCodecs(),
                                                         censor.CensorPaths),
                 S_OK);
        CreateArchiveDialog form(fixture_.path(), {input}, nullptr, catalog,
                                 sourceBound ? &source : nullptr);
        combo(form, "archiveFormat")->setCurrentText("tar");
        const auto before = form.sourceInfo().SymLinks;
        form.show();
        bool edited = false;
        QVERIFY(EditArchiveMetadata(form, false, [&](QDialog &editor) {
            auto *links = editor.findChild<QCheckBox *>("IDX_COMPRESS_NT_SYM_LINKS");
            QVERIFY(links && links->isVisible() && links->isEnabled());
            links->setChecked(!before.Val);
            edited = true;
            // Inline edits stay within Expert until its Apply.
            QCOMPARE(form.sourceInfo().SymLinks.Val, before.Val);
        }));
        QVERIFY(edited);
        QCOMPARE(form.sourceInfo().SymLinks.Val, before.Val);
        QCOMPARE(form.sourceInfo().SymLinks.Def, before.Def);
        QVERIFY(!QFile::exists(settings_));
    }
    void tarMethodChangesKeepPendingTimeOverrides() {
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        combo(form, "archiveFormat")->setCurrentText("tar");
        form.show();
        QVERIFY(EditArchiveMetadata(form, true, [&](QDialog &editor) {
            auto *method = combo(editor, "compressionMethod");
            const int posix = method->findData(int(NCompressDialog::kPosix));
            // The source represents its default GNU method with the Auto item.
            const int gnu = method->findData(-1);
            QVERIFY(posix >= 0 && gnu >= 0);
            method->setCurrentIndex(posix);
            auto *overrideTime = editor.findChild<QCheckBox *>("IDX_COMPRESS_ATIME_SET");
            auto *accessTime = editor.findChild<QCheckBox *>("IDX_COMPRESS_ATIME");
            QVERIFY(overrideTime->isVisible() && accessTime->isVisible());
            if (!overrideTime->isChecked())
                overrideTime->click();
            accessTime->setChecked(true);
            method->setCurrentIndex(gnu);
            QVERIFY(accessTime->isHidden());
            method->setCurrentIndex(posix);
            QVERIFY(accessTime->isVisible());
            QVERIFY(overrideTime->isChecked() && accessTime->isChecked());
        }));
        QVERIFY(form.compressionOptions().ATime.Def);
        QVERIFY(form.compressionOptions().ATime.Val);
        QVERIFY(!QFile::exists(settings_));
        QVERIFY(EditArchiveMetadata(form, false, [&](QDialog &editor) {
            editor.findChild<QCheckBox *>("IDX_COMPRESS_ATIME")->setChecked(false);
        }));
        QVERIFY(form.compressionOptions().ATime.Val);
    }
    void languageChangesKeepLiveEditorAndCompactActions() {
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        form.show();
        QVERIFY(EditArchiveExpert(form, false, [&](QDialog &editor) {
            auto *method = combo(editor, "compressionMethod");
            method->setCurrentText("PPMd");
            text(editor, "archiveParameters")->setText(QStringLiteral("  mt=1  "));
            const auto selected = properties(form.sourceInfo());
            for (const char *language : {"ru", "-"}) {
                SaveRegLang(UString(language), settings_);
                ReloadLang(settings_);
                QtNotifyLanguageChanged();
                QTRY_COMPARE(editor.windowTitle(),
                             QCoreApplication::translate("ArchiveRedesign", "Expert options"));
                auto *buttons = editor.findChild<QDialogButtonBox *>();
                QTRY_COMPARE(buttons->button(QDialogButtonBox::Ok)->text(),
                             QCoreApplication::translate("ArchiveRedesign", "Apply"));
                QCOMPARE(properties(form.sourceInfo()), selected);
                QCOMPARE(text(editor, "archiveParameters")->text(), QString("  mt=1  "));
                for (const QSize size : {QSize(640, 480), QSize(360, 260)}) {
                    editor.resize(size);
                    auto *scroll = editor.findChild<QScrollArea *>("dialogContentScroll");
                    for (const char *name : {"compressionMethod", "compressionDictionary",
                                             "compressionMemoryLimit", "archiveParameters"}) {
                        auto *field = editor.findChild<QWidget *>(name);
                        scroll->ensureWidgetVisible(field);
                        QTRY_VERIFY2(UiInspection::fullyVisible(*field, editor), name);
                        QTRY_VERIFY(UiInspection::fullyVisible(*buttons, editor));
                    }
                    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                    QVERIFY(UiInspection::writeReport(
                        editor,
                        QString("archive-expert-language-%1-%2").arg(language).arg(size.width())));
                    auto *metadata = editor.findChild<QtDisclosure *>("archiveMetadataSection");
                    metadata->setExpanded(true);
                    auto *time = editor.findChild<QCheckBox *>("IDX_COMPRESS_CTIME");
                    QApplication::processEvents();
                    scroll->ensureWidgetVisible(time);
                    QTRY_VERIFY(UiInspection::fullyVisible(*time, editor));
                    QVERIFY(time->width() >= time->sizeHint().width());
                    QVERIFY(time->height() >= time->sizeHint().height());
                    QTRY_VERIFY(UiInspection::fullyVisible(*buttons, editor));
                    QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                    QVERIFY(UiInspection::writeReport(
                        editor,
                        QString("archive-expert-metadata-%1-%2").arg(language).arg(size.width())));
                    metadata->setExpanded(false);
                }
            }
        }));
    }
    void unsupportedMethodFieldsDoNotOccupyTheEditor() {
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        combo(form, "archiveFormat")->setCurrentText("zip");
        form.show();
        QVERIFY(EditArchiveExpert(form, true, [&](QDialog &editor) {
            combo(editor, "compressionMethod")->setCurrentText("ZSTD");
            QCOMPARE(combo(editor, "compressionMethod")->currentText(), QString("ZSTD"));
            QVERIFY(!combo(editor, "compressionDictionary")->isVisible());
            QVERIFY(!combo(editor, "compressionWordSize")->isVisible());
            QVERIFY(UiInspection::writeReport(editor, "archive-expert-zstd"));
        }));
        QCOMPARE(QString::fromWCharArray(form.sourceInfo().Method.Ptr()), QString("ZSTD"));
    }
    void cancellingSfxChangeRestoresEditedArchiveName() {
        const auto catalog = NativeArchiveFixture::Catalog();
        const auto module = catalog->sfxModule();
        if (!module.available())
            QSKIP(qPrintable(module.error()));
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, catalog);
        form.show();
        QVERIFY(EditArchiveExpert(form, true, [&](QDialog &editor) {
            auto *sfx = editor.findChild<QCheckBox *>("archiveSfx");
            QVERIFY(sfx->isEnabled());
            sfx->setChecked(true);
        }));
        QVERIFY(form.sourceInfo().SFXMode);
        const QString custom = QStringLiteral("своё имя 空.exe");
        text(form, "archiveName")->setText(custom);
        QVERIFY(EditArchiveExpert(form, false, [&](QDialog &editor) {
            editor.findChild<QCheckBox *>("archiveSfx")->setChecked(false);
            QVERIFY(!form.sourceInfo().SFXMode);
            QVERIFY(text(form, "archiveName")->text() != custom);
        }));
        QVERIFY(form.sourceInfo().SFXMode);
        QCOMPARE(text(form, "archiveName")->text(), custom);
        QVERIFY(!QFile::exists(settings_));
    }
    void inputsGrowWithTheirForms() {
        CreateArchiveDialog form(fixture_.path(), {}, nullptr, NativeArchiveFixture::Catalog());
        form.show();
        auto *additional = form.findChild<QtDisclosure *>("archiveAdditionalSection");
        additional->setExpanded(true);
        form.resize(640, form.height());
        QApplication::processEvents();
        auto *volume = combo(form, "archiveVolumeSize");
        const int narrowVolume = volume->width();
        form.resize(form.width() + 160, form.height());
        QTRY_VERIFY(volume->width() > narrowVolume);
        QVERIFY(EditArchiveExpert(form, false, [&](QDialog &editor) {
            editor.resize(360, 300);
            QApplication::processEvents();
            auto *parameters = text(editor, "archiveParameters");
            const int narrowParameters = parameters->width();
            editor.resize(editor.width() + 160, editor.height());
            QTRY_VERIFY(parameters->width() > narrowParameters);
        }));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CompressionExpertTests)
#include "compression_expert_tests.moc"
