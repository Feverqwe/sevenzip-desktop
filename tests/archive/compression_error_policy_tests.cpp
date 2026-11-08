#include "7zip/UI/GUI/CompressDialogRes.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

extern bool g_DisableUserQuestions;

namespace {
class CMessages {
    QTimer _timer;

  public:
    QStringList Texts;
    CMessages(const QString &tag, bool question = false,
              QMessageBox::StandardButton answer = QMessageBox::No) {
        QObject::connect(&_timer, &QTimer::timeout, [this, tag, question, answer] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            const auto close =
                qScopeGuard([&] { box->done(question ? int(answer) : int(QMessageBox::Ok)); });
            Texts << box->text();
#ifdef Q_OS_MACOS
            QCOMPARE(box->windowTitle(), QString());
#else
            QCOMPARE(box->windowTitle(), QString("7-Zip"));
#endif
            QCOMPARE(box->icon(), question ? QMessageBox::Question : QMessageBox::Critical);
            QCOMPARE(box->standardButtons(),
                     question ? QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No |
                                                             QMessageBox::Cancel)
                              : QMessageBox::StandardButtons(QMessageBox::Ok));
            for (const QSize size : {QSize(480, 220), QSize(320, 160)}) {
                box->resize(size);
                QApplication::processEvents();
                auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                QVERIFY(label && UiInspection::fullyVisible(*label, *box));
                QVERIFY(UiInspection::labelFits(*label));
                for (auto *button : box->buttons())
                    QVERIFY(UiInspection::fullyVisible(*button, *box));
                QVERIFY(UiInspection::writeReport(*box, tag + '-' + QString::number(size.width())));
            }
        });
        _timer.start(1);
    }
};

void addData(const QStringList &cases) {
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("suppressed");
    QTest::addColumn<QString>("language");
    for (const auto &scenario : cases)
        for (bool suppressed : {false, true})
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(
                    qPrintable(QString("%1-%2-%3").arg(scenario).arg(suppressed).arg(language)))
                    << scenario << suppressed << language;
}
void submit(CreateArchiveDialog &dialog) {
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
} // namespace

class CompressionErrorPolicyTests final : public QObject {
    Q_OBJECT
    QTemporaryDir _fixture;
    QByteArray _previousSettings;
    QString _settings;

    void language(const QString &name) {
        SaveRegLang(UString(name.toStdWString().c_str()), _settings);
        ReloadLang(_settings);
    }

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
    void refusalAndRecovery_data() {
        addData({"ascii", "length", "mismatch", "path", "volume", "volume-before-path"});
    }
    void refusalAndRecovery() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; writer recovery unverified");
#else
        QFETCH(QString, scenario);
        QFETCH(bool, suppressed);
        QFETCH(QString, language);
        this->language(language);
        const bool previous = g_DisableUserQuestions;
        const auto restore = qScopeGuard([&] { g_DisableUserQuestions = previous; });
        g_DisableUserQuestions = suppressed;
        QTemporaryDir fixture;
        const QString input = fixture.filePath(QStringLiteral("данные 空.bin"));
        const QByteArray bytes("original\0bytes", 14);
        QVERIFY(SourceExtractFixture::writeFile(input, bytes));
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        CreateArchiveDialog dialog(fixture.path(), {input}, nullptr, catalog);
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *confirmation = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        name->setText(QStringLiteral("отказ 空.zip"));
        QString expected;
        if (scenario == "ascii") {
            password->setText(QStringLiteral("пароль"));
            confirmation->setText(password->text());
            expected = QString::fromWCharArray(LangString(IDS_PASSWORD_USE_ASCII));
        } else if (scenario == "length") {
            dialog.findChild<QComboBox *>("archiveEncryption")->setCurrentIndex(1);
            password->setText(QString(100, 'x'));
            confirmation->setText(password->text());
            expected = QString::fromWCharArray(LangString(IDS_PASSWORD_TOO_LONG));
        } else if (scenario == "mismatch") {
            password->setText("first");
            confirmation->setText("second");
            expected = QString::fromWCharArray(LangString(IDS_PASSWORD_NOT_MATCH));
        } else if (scenario == "volume" || scenario == "volume-before-path") {
            if (scenario == "volume-before-path")
                name->setText(QString("../").repeated(256) + "refused.zip");
            volume->setEditText("invalid volume");
            expected = QString::fromWCharArray(LangString(IDS_INCORRECT_VOLUME_SIZE));
        } else {
            name->setText(QString("../").repeated(256) + "refused.zip");
            expected = QString::fromUtf8(language == "ru" ? "Неверный путь к архиву"
                                                          : "Incorrect archive path");
        }
        const QByteArray before = SourceExtractFixture::readFile(_settings);
        const QString tag =
            QString("compress-error-%1-%2-%3").arg(scenario).arg(suppressed).arg(language);
        CMessages messages(tag);
        dialog.show();
        submit(dialog);
        QCOMPARE(messages.Texts, suppressed ? QStringList() : QStringList{expected});
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(dialog.target().isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(_settings), before);
        QCOMPARE(SourceExtractFixture::readFile(input), bytes);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Files),
                 QStringList{QFileInfo(input).fileName()});
        password->clear();
        confirmation->clear();
        volume->setEditText({});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        name->setText(QStringLiteral("готовый 空.zip"));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), {input}, {}, false,
                                               dialog.compressionOptions()),
                 S_OK);
        done.clear();
        backend.list(dialog.target());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        done.clear();
        backend.test(dialog.target());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        QCOMPARE(SourceExtractFixture::readFile(input), bytes);
#endif
    }
    void memoryRefusal_data() { addData({"memory"}); }
    void memoryRefusal() {
        QFETCH(bool, suppressed);
        QFETCH(QString, language);
        this->language(language);
        const bool previous = g_DisableUserQuestions;
        const auto restore = qScopeGuard([&] { g_DisableUserQuestions = previous; });
        g_DisableUserQuestions = suppressed;
        NCompression::CInfo registry;
        auto &profile = registry.Formats.AddNew();
        profile.FormatID = "7z";
        profile.MemUse = "1M";
        registry.Save(_settings);
        const QByteArray before = SourceExtractFixture::readFile(_settings);
        CreateArchiveDialog dialog(_fixture.path());
        CMessages messages(QString("compress-error-memory-%1-%2").arg(suppressed).arg(language));
        bool memoryEditorOpened = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &dialog, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!editor || editor->objectName() != "archiveExpertDialog")
                return;
            responder.stop();
            const auto close = qScopeGuard([&] { editor->reject(); });
            auto *limit = editor->findChild<QComboBox *>("compressionMemoryLimit");
            QVERIFY(limit && limit->isVisible());
            QCOMPARE(limit->currentData().toString(), QString("1M"));
            memoryEditorOpened = true;
        });
        responder.start(1);
        submit(dialog);
        QVERIFY(memoryEditorOpened);
        QCOMPARE(messages.Texts.size(), 1);
        QVERIFY(messages.Texts[0].contains(
            QString::fromWCharArray(LangString(IDS_MEM_OPERATION_BLOCKED))));
        QVERIFY(dialog.target().isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(_settings), before);
        dialog.reject();
    }
    void fallbackMemoryLimit_data() {
        QTest::addColumn<bool>("known");
        QTest::addColumn<bool>("explicitLimit");
        for (bool known : {false, true})
            for (bool explicitLimit : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(known).arg(explicitLimit)))
                    << known << explicitLimit;
    }
    void fallbackMemoryLimit() {
        QFETCH(bool, known);
        QFETCH(bool, explicitLimit);
        NCompressDialog::CSystemInfo system;
        system.RamSizeDefined = known;
        system.RamSize = system.RamSizeReduced = 4 << 20;
        system.RamUsageAuto = 1 << 20;
        NCompression::CMemUse limit;
        if (explicitLimit)
            limit.Parse("1M");
        UString message;
        QVERIFY(!CCompressDialog::OnOK(64 << 20, system, limit, message));
        QVERIFY(!message.IsEmpty());
        message.Empty();
        QVERIFY(CCompressDialog::OnOK(1 << 20, system, limit, message));
        QVERIFY(message.IsEmpty());
        QVERIFY(CCompressDialog::OnOK(UInt64(-1), system, limit, message));
    }
    void smallVolumeQuestion_data() { addData({"no", "cancel"}); }
    void smallVolumeQuestion() {
        QFETCH(QString, scenario);
        QFETCH(bool, suppressed);
        QFETCH(QString, language);
        this->language(language);
        const bool previous = g_DisableUserQuestions;
        const auto restore = qScopeGuard([&] { g_DisableUserQuestions = previous; });
        g_DisableUserQuestions = suppressed;
        const QByteArray before = SourceExtractFixture::readFile(_settings);
        CreateArchiveDialog dialog(_fixture.path());
        dialog.findChild<QComboBox *>("archiveVolumeSize")->setEditText("1K");
        CMessages messages(
            QString("compress-question-%1-%2-%3").arg(scenario).arg(suppressed).arg(language), true,
            scenario == "no" ? QMessageBox::No : QMessageBox::Cancel);
        submit(dialog);
        QCOMPARE(messages.Texts.size(), 1);
        QVERIFY(dialog.target().isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(_settings), before);
        dialog.reject();
    }
};
QTEST_MAIN(CompressionErrorPolicyTests)
#include "compression_error_policy_tests.moc"
