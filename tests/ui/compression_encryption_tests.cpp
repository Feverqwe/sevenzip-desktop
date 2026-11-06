#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class CompressionEncryptionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QString settings_;
    QString source_;
    QByteArray oldSettings_;

    static void submit(CreateArchiveDialog &dialog) {
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    }
    static void setPassword(CreateArchiveDialog &dialog, const QString &password) {
        dialog.findChild<QLineEdit *>("archivePassword")->setText(password);
        dialog.findChild<QLineEdit *>("archivePasswordConfirmation")->setText(password);
    }

  private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = directory_.filePath("encryption.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
        source_ = directory_.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source_);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(8192, 'z')), qint64(8192));
    }
    void init() { QFile::remove(settings_); }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void upstreamPasswordBoundaries() {
        using P = CCompressDialog;
        QCOMPARE(P::CheckPassword("", "", false, u"zip", ""), P::kPasswordOK);
        QCOMPARE(P::CheckPassword("one", "two", false, u"7z", ""), P::kPasswordNotMatch);
        QCOMPARE(P::CheckPassword("one", "two", true, u"7z", ""), P::kPasswordOK);
        const QString unicode = QStringLiteral("пароль 密碼");
        QCOMPARE(P::CheckPassword(unicode, unicode, false, u"7z", ""), P::kPasswordOK);
        QCOMPARE(P::CheckPassword(unicode, unicode, true, u"zip", ""), P::kPasswordUseAscii);
        QCOMPARE(P::CheckPassword("line\n", "line\n", true, u"zip", "AES256"),
                 P::kPasswordUseAscii);
        QCOMPARE(P::CheckPassword(QString(QChar(0x1f)), {}, true, u"zip", ""),
                 P::kPasswordUseAscii);
        QCOMPARE(P::CheckPassword(QString(QChar(0x7f)), {}, true, u"zip", ""), P::kPasswordOK);
        QCOMPARE(P::CheckPassword(QString(99, 'x'), {}, true, u"zip", "aes256"), P::kPasswordOK);
        QCOMPARE(P::CheckPassword(QString(100, 'x'), {}, true, u"zip", "AES256"),
                 P::kPasswordTooLong);
        QCOMPARE(P::CheckPassword(QString(100, 'x'), {}, true, u"zip", ""), P::kPasswordOK);
    }
    void methodsAndShowPassword() {
        CreateArchiveDialog dialog(directory_.path(), {source_});
        auto *method = dialog.findChild<QComboBox *>("archiveEncryption");
        auto *headers = dialog.findChild<QCheckBox *>("archiveEncryptHeaders");
        auto *show = dialog.findChild<QCheckBox *>("archiveShowPassword");
        auto *repeat = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        auto *form = qobject_cast<QFormLayout *>(
            dialog.findChild<QGroupBox *>("archiveEncryptionGroup")->layout());
        QWidget *repeatLabel = form->labelForField(repeat);
        QVERIFY(repeatLabel);
        dialog.show();
        dialog.findChild<QCheckBox *>("archivePasswordEnabled")->setChecked(true);
        QCOMPARE(method->currentText(), QString("AES-256"));
        QVERIFY(!method->isVisible());
        QVERIFY(headers->isEnabled());
        QVERIFY(dialog.compressionOptions().EncryptionMethod.isEmpty());
        headers->setChecked(true);
        setPassword(dialog, "shown password");
        repeat->setText("different");
        show->setChecked(true);
        QVERIFY(!repeat->isVisible());
        QVERIFY(!repeatLabel->isVisible());
        QCOMPARE(repeat->echoMode(), QLineEdit::Normal);
        const QString shownReport = UiInspection::dump(dialog);
        QVERIFY(!shownReport.contains("shown password"));
        QVERIFY(!shownReport.contains("different"));
        show->setChecked(false);
        QVERIFY(repeat->isVisible());
        QVERIFY(repeatLabel->isVisible());
        QCOMPARE(repeat->text(), QString("different"));
        QCOMPARE(repeat->echoMode(), QLineEdit::Password);
        const QString maskedReport = UiInspection::dump(dialog);
        QVERIFY(!maskedReport.contains("shown password"));
        QVERIFY(!maskedReport.contains("different"));
        show->setChecked(true);
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        QVERIFY(method->isVisible());
        QVERIFY(!headers->isEnabled());
        QVERIFY(!dialog.encryptHeaders());
        QCOMPARE(method->currentText(), QString("ZipCrypto"));
        method->setCurrentText("AES-256");
        QCOMPARE(dialog.compressionOptions().EncryptionMethod, QString("AES256"));
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("7z");
        QVERIFY(dialog.encryptHeaders());
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        QCOMPARE(method->currentText(), QString("AES-256"));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        CreateArchiveDialog restored(directory_.path(), {source_});
        QCOMPARE(restored.findChild<QComboBox *>("archiveEncryption")->currentText(),
                 QString("AES-256"));
        QVERIFY(restored.findChild<QCheckBox *>("archiveShowPassword")->isChecked());
        QVERIFY(restored.password().isEmpty());
        restored.findChild<QComboBox *>("archiveFormat")->setCurrentText("7z");
        QVERIFY(!restored.encryptHeaders());
        restored.findChild<QCheckBox *>("archivePasswordEnabled")->setChecked(true);
        QVERIFY(restored.encryptHeaders());
        QFile settings(settings_);
        QVERIFY(settings.open(QIODevice::ReadOnly));
        QVERIFY(!settings.readAll().contains("shown password"));
        restored.reject();
    }
    void invalidAndCancelledDoNotSave() {
        CreateArchiveDialog dialog(directory_.path(), {source_});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        dialog.findChild<QComboBox *>("archiveEncryption")->setCurrentText("AES-256");
        setPassword(dialog, QString(100, 'x'));
        dialog.findChild<QCheckBox *>("archiveShowPassword")->setChecked(true);
        QTimer::singleShot(0, &dialog, [] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                warning->accept();
        });
        submit(dialog);
        QCOMPARE(dialog.result(), 0);
        QVERIFY(!QFileInfo::exists(settings_));
        dialog.reject();
        CreateArchiveDialog restored(directory_.path());
        QCOMPARE(restored.findChild<QComboBox *>("archiveFormat")->currentText(), QString("7z"));
        QVERIFY(!restored.findChild<QCheckBox *>("archiveShowPassword")->isChecked());
    }
    void disabledPasswordDraftDoesNotBlockSubmission() {
        const auto catalog = NativeArchiveFixture::Catalog();
        QVERIFY(catalog && catalog->result() == S_OK);
        CreateArchiveDialog dialog(directory_.path(), {source_}, nullptr, catalog);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *confirmation = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        auto *protect = dialog.findChild<QCheckBox *>("archivePasswordEnabled");
        format->setCurrentText("7z");
        password->setText("draft password");
        confirmation->setText("different draft");
        protect->setChecked(false);
        QCOMPARE(format->currentText(), QString("7z"));
        QVERIFY(!password->isEnabled());
        QVERIFY(dialog.password().isEmpty());
        bool errorShown = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &dialog, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errorShown = true;
                error->accept();
            }
        });
        dismiss.start(5);
        submit(dialog);
        dismiss.stop();
        QVERIFY(!errorShown);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(dialog.sourceInfo().Password.isEmpty());
        QCOMPARE(password->text(), QString("draft password"));
        QCOMPARE(confirmation->text(), QString("different draft"));
    }
    void unsupportedFormatKeepsProtectionUntilExplicitDecision_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("decision");
        for (const QString &language : {QString("-"), QString("ru")})
            for (int decision = 0; decision < 3; ++decision)
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(decision)))
                    << language << decision;
    }
    void unsupportedFormatKeepsProtectionUntilExplicitDecision() {
        QFETCH(QString, language);
        QFETCH(int, decision);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(L"-", settings_);
            ReloadLang(settings_);
        });
        QFile settingsFile(settings_);
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        const QByteArray before = settingsFile.readAll();
        settingsFile.close();
        const auto catalog = NativeArchiveFixture::Catalog();
        QVERIFY(catalog && catalog->result() == S_OK);
        CreateArchiveDialog dialog(directory_.path(), {source_}, nullptr, catalog);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *protect = dialog.findChild<QCheckBox *>("archivePasswordEnabled");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *warning = dialog.findChild<QLabel *>("archiveProtectionWarning");
        auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        format->setCurrentText("7z");
        setPassword(dialog, "draft password");
        dialog.show();
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        format->setCurrentText("tar");
        QVERIFY(protect->isChecked());
        QVERIFY(protect->isEnabled());
        QVERIFY(protect->isVisible());
        QVERIFY(warning->isVisible());
        QCOMPARE(warning->text(),
                 QCoreApplication::translate(
                     "ArchiveRedesign", "This format does not support password protection. Choose "
                                        "another format or turn off password protection."));
        QVERIFY(!create->isEnabled());
        submit(dialog);
        QCOMPARE(accepted.size(), 0);
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        scroll->ensureWidgetVisible(warning);
        QTRY_VERIFY(UiInspection::fullyVisible(*warning, dialog));
        QVERIFY(UiInspection::writeReport(
            dialog, QString("create-protection-unavailable-%1-%2").arg(language).arg(decision)));
        if (decision == 2) {
            dialog.reject();
            QVERIFY(settingsFile.open(QIODevice::ReadOnly));
            QCOMPARE(settingsFile.readAll(), before);
            QCOMPARE(accepted.size(), 0);
            return;
        }
        if (decision == 0) {
            format->setCurrentText("7z");
            QVERIFY(protect->isChecked());
            QCOMPARE(password->text(), QString("draft password"));
            QVERIFY(password->isVisible());
        } else
            protect->setChecked(false);
        QVERIFY(!warning->isVisible());
        QVERIFY(create->isEnabled());
        submit(dialog);
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(dialog.sourceInfo().Password,
                 decision == 0 ? QString("draft password") : QString());
        QCOMPARE(format->currentText(), decision == 0 ? QString("7z") : QString("tar"));
    }
    void realArchives_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("encryption");
        QTest::addColumn<QString>("password");
        QTest::addColumn<bool>("headers");
        QTest::newRow("7z-unicode-headers")
            << QString("7z") << QString("AES-256") << QStringLiteral("пароль 密碼") << true;
        QTest::newRow("zip-crypto")
            << QString("zip") << QString("ZipCrypto") << QString(100, 'x') << false;
        QTest::newRow("zip-aes-boundary")
            << QString("zip") << QString("AES-256") << QString(99, 'x') << false;
        QTest::newRow("unencrypted") << QString("zip") << QString("AES-256") << QString() << false;
    }
    void realArchives() {
        QFETCH(QString, format);
        QFETCH(QString, encryption);
        QFETCH(QString, password);
        QFETCH(bool, headers);
        CreateArchiveDialog dialog(directory_.path(), {source_});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText(format);
        dialog.findChild<QComboBox *>("archiveEncryption")->setCurrentText(encryption);
        dialog.findChild<QCheckBox *>("archiveEncryptHeaders")->setChecked(headers);
        setPassword(dialog, password);
        dialog.findChild<QLineEdit *>("archiveName")
            ->setText(QString::fromLatin1(QTest::currentDataTag()) + '.' + format);
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(),
                                               dialog.password(), dialog.encryptHeaders(),
                                               dialog.compressionOptions()),
                 S_OK);
        QProcess process;
        process.start("7z", {"l", "-slt", "-p" + password, "--", dialog.target()});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const QByteArray listing = process.readAllStandardOutput();
        QVERIFY(listing.contains(password.isEmpty() ? "Encrypted = -" : "Encrypted = +"));
        if (format == "zip" && !password.isEmpty())
            QVERIFY(listing.contains(encryption == "ZipCrypto" ? "ZipCrypto" : "AES-256"));
        if (headers) {
            process.start("7z", {"l", "-slt", "-pwrong", "--", dialog.target()});
            QVERIFY(process.waitForFinished(30000));
            QVERIFY(process.exitCode() != 0);
            QVERIFY(
                !process.readAllStandardOutput().contains(QFileInfo(source_).fileName().toUtf8()));
        }
        const QString output =
            directory_.filePath(QString::fromLatin1(QTest::currentDataTag()) + "-out");
        process.start("7z", {"x", "-p" + password, "-o" + output, "--", dialog.target()});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        QFile file(QDir(output).filePath(QFileInfo(source_).fileName()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(8192, 'z'));
    }
    void backendFailureAndCancel() {
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        CompressionOptions options;
        options.EncryptionMethod = "AES256";
        const QString target = directory_.filePath("rejected.zip");
        QVERIFY(SourceCreationFixture::Create(backend, target, {source_}, QString(100, 'x'), false,
                                              options) != S_OK);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(SourceCreationFixture::Create(backend, target, {source_}, QStringLiteral("пароль"),
                                              false, options) != S_OK);
        options.EncryptionMethod = "unsupported";
        QVERIFY(SourceCreationFixture::Create(backend, target, {source_}, "abc", false, options) !=
                S_OK);
        options.EncryptionMethod = "AES256";
        QVERIFY(SourceCreationFixture::Create(backend, directory_.filePath("invalid.7z"), {source_},
                                              "abc", false, options) != S_OK);
        QVERIFY(SourceCreationFixture::Create(backend, target, {source_}, "abc", true, options) !=
                S_OK);
        QCOMPARE(
            SourceCreationFixture::Create(backend, target, {source_}, "abc", false, options, true),
            E_ABORT);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QDir(directory_.path())
                    .entryList({"*.stage-*", "*.sevenzip-*"}, QDir::Files | QDir::Hidden)
                    .isEmpty());
    }
    void geometry() {
        NCompression::CInfo registry;
        registry.ArcPaths.Add(directory_.filePath(QString(400, QChar(0x044f)) + ".7z"));
        registry.Save(settings_);
        for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
            for (const bool shown : {false, true}) {
                CreateArchiveDialog dialog(directory_.path());
                dialog.findChild<QCheckBox *>("archivePasswordEnabled")->setChecked(true);
                dialog.resize(size);
                dialog.findChild<QCheckBox *>("archiveShowPassword")->setChecked(shown);
                dialog.show();
                QApplication::processEvents();
                auto *group = dialog.findChild<QGroupBox *>("archiveEncryptionGroup");
                dialog.findChild<QScrollArea *>()->ensureWidgetVisible(group);
                QApplication::processEvents();
                auto *method = dialog.findChild<QComboBox *>("archiveEncryption");
                auto *headers = dialog.findChild<QCheckBox *>("archiveEncryptHeaders");
                auto *password = dialog.findChild<QLineEdit *>("archivePassword");
                QVERIFY2(UiInspection::fullyVisible(*group, dialog),
                         qPrintable(UiInspection::dump(dialog)));
                QVERIFY(UiInspection::separate(*password, *headers, dialog));
                if (method->isVisible()) {
                    QVERIFY(UiInspection::separate(*password, *method, dialog));
                    QVERIFY(UiInspection::separate(*method, *headers, dialog));
                }
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("encryption-%1-%2").arg(size.width()).arg(shown)));
            }
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CompressionEncryptionTests)
#include "compression_encryption_tests.moc"
