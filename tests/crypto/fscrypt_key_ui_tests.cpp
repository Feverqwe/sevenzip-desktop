#include "C/7zVersion.h"
#include "archive/backend/archivebackend.h"
#include "crypto/fscrypt/fscryptkeydialog.h"
#include "devices/deviceselectiondialog.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "localization/qtlanguageservices.h"
#include "platform/cpu/CpuArch.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {
class MemoryDevice final : public DeviceReadHandle {
    const QByteArray bytes_;

  public:
    explicit MemoryDevice(QByteArray bytes) : bytes_(std::move(bytes)) {}
    qint32 readAt(quint64 offset, void *data, quint32 size, quint32 &read) override {
        read = 0;
        if (offset > quint64(bytes_.size()) || size > quint64(bytes_.size()) - offset)
            return E_FAIL;
        memcpy(data, bytes_.constData() + offset, size);
        read = size;
        return S_OK;
    }
};
int Row(const FileTable &table, const QString &name) {
    for (int i = 0; i < table.rowCount(); ++i)
        if (table.model()->index(i, 0).isValid() &&
            table.model()->index(i, 0).data().toString() == name)
            return i;
    return -1;
}
} // namespace

class FscryptKeyUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void validationMultipleKeysAndCancel() {
        FscryptKeyDialog dialog;
        dialog.show();
        auto *open = dialog.findChild<QPushButton *>("fscryptOpen");
        auto *key = dialog.findChild<QLineEdit *>("fscryptMasterKey");
        QVERIFY(!open->isEnabled());
        key->setText(QString(31, 'a'));
        QVERIFY(!open->isEnabled());
        QTest::keyClick(key, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.keyDescription().isEmpty());
        key->setText(QString(32, 'z'));
        QVERIFY(!open->isEnabled());
        key->setText(QString(16, 'A') + " " + QString(16, 'B'));
        QVERIFY(open->isEnabled());
        auto *show = dialog.findChild<QCheckBox *>("fscryptShowKeys");
        show->click();
        QCOMPARE(key->echoMode(), QLineEdit::Normal);
        QVERIFY(!UiInspection::dump(dialog).contains(QString(16, 'A')));
        dialog.findChild<QPushButton *>("fscryptAddKey")->click();
        QVERIFY(!open->isEnabled());
        const auto versions = dialog.findChildren<QComboBox *>("fscryptVersion");
        const auto edits = dialog.findChildren<QLineEdit *>("fscryptMasterKey");
        const auto descriptors = dialog.findChildren<QLineEdit *>("fscryptDescriptor");
        versions[1]->setCurrentIndex(1);
        edits[1]->setText(QString(32, 'c'));
        descriptors[1]->setText("01 23 45 67 89 ab cd ef");
        QVERIFY(open->isEnabled());
        dialog.findChild<QPushButton *>("fscryptAddKey")->click();
        dialog.findChildren<QComboBox *>("fscryptVersion")[2]->setCurrentIndex(1);
        dialog.findChildren<QLineEdit *>("fscryptMasterKey")[2]->setText(QString(32, 'd'));
        dialog.findChildren<QLineEdit *>("fscryptDescriptor")[2]->setText("0123456789abcdef");
        QVERIFY(!open->isEnabled());
        dialog.findChildren<QPushButton *>("fscryptRemoveKey")[2]->click();
        QVERIFY(open->isEnabled());
        QTest::keyClick(edits[1], Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.keyDescription(), "fscrypt:v2:" + QString(16, 'a') + QString(16, 'b') +
                                              ";v1:0123456789abcdef:" + QString(32, 'c'));
        FscryptKeyDialog rejected;
        rejected.findChild<QLineEdit *>("fscryptMasterKey")->setText(QString(32, 'd'));
        rejected.show();
        QTest::keyClick(&rejected, Qt::Key_Escape);
        QVERIFY(rejected.keyDescription().isEmpty());
    }
    void geometry_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("state");
        for (const QString &language : {"en", "ru"})
            for (int width : {560, 460})
                for (const QString &state : {"v2", "v1", "invalid-v1"})
                    QTest::newRow(qPrintable(language + QString::number(width) + '-' + state))
                        << language << width << state;
    }
    void geometry() {
        QFETCH(QString, language);
        QFETCH(int, width);
        QFETCH(QString, state);
        QtLoadTranslation(language == "en" ? "-" : "ru");
        FscryptKeyDialog dialog;
        dialog.findChild<QComboBox *>("fscryptVersion")->setCurrentIndex(state == "v2" ? 0 : 1);
        if (state == "invalid-v1") {
            dialog.findChild<QLineEdit *>("fscryptMasterKey")->setText(QString(32, 'c'));
            dialog.findChild<QLineEdit *>("fscryptDescriptor")->setText("bad");
        }
        dialog.resize(width, 490);
        dialog.show();
        QCoreApplication::processEvents();
        dialog.layout()->activate();
        for (auto *label : dialog.findChildren<QLabel *>())
            if (label->isVisible()) {
                QVERIFY(UiInspection::fullyVisible(*label, dialog));
                QVERIFY(UiInspection::labelFits(*label));
            }
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->isVisible())
                QVERIFY(UiInspection::fullyVisible(*button, dialog));
        dialog.setProperty("uiInspection_language", language);
        dialog.setProperty("uiInspection_state", state);
        QVERIFY(UiInspection::writeReport(dialog, "fscrypt-keys-" + language +
                                                      QString::number(width) + '-' + state));
        dialog.findChild<QPushButton *>("fscryptWithoutKeys")->click();
        QCOMPARE(dialog.keyDescription(), QString("fscrypt:none"));
        QtLoadTranslation("-");
    }
    void encryptedSourceOpening_data() {
        QTest::addColumn<bool>("device");
        QTest::addColumn<QString>("mode");
        for (bool device : {false, true})
            for (const QString &mode : {"keys", "none", "cancel"})
                QTest::newRow(qPrintable((device ? QString("device-") : QString("image-")) + mode))
                    << device << mode;
    }
    void encryptedSourceOpening() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module required");
#else
        QFETCH(bool, device);
        QFETCH(QString, mode);
        QFile manifest(QStringLiteral(FSCRYPT_FIXTURES "/native.json"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto record = QJsonDocument::fromJson(manifest.readAll()).object();
        const auto cases = record["cases"].toArray();
        QFile packed(QStringLiteral(FSCRYPT_FIXTURES "/native.ext4.zlib"));
        QVERIFY(packed.open(QIODevice::ReadOnly));
        QByteArray compressed(4, '\0');
        SetBe32(compressed.data(), UInt32(record["image_size"].toDouble()));
        compressed += packed.readAll();
        const QByteArray bytes = qUncompress(compressed);
        QVERIFY(!bytes.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString image = directory.filePath("encrypted ext 空.img");
        QFile file(image);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        const QString engine = QFileInfo(module).dir().filePath(
#ifdef Q_OS_WIN
            "7z.exe"
#else
            "7z"
#endif
        );
        ArchiveBackend backend(nullptr, engine);
        // Use the real fscrypt reader directly; this dialog suite does not
        // measure discovery of the unrelated format modules.
        QCOMPARE(backend.loadCodecCatalog(QStringLiteral(FSCRYPT_MODULE),
                                          (MY_VER_MAJOR << 16) | MY_VER_MINOR),
                 S_OK);
        const ExternalDevice description{
            "fixture", "Encrypted ext 空", "fixture", quint64(bytes.size()), 512, false};
        DeviceSelectionServices services{
            [description](QString &) { return QList<ExternalDevice>{description}; },
            [bytes](const ExternalDevice &, const auto &) {
                return DeviceAccessResult{
                    DeviceAccessStatus::Ready, {}, std::make_shared<MemoryDevice>(bytes)};
            }};
        MainWindow window(&backend, {}, nullptr, nullptr, services);
        window.openPath(directory.path());
        window.show();
        unsigned prompts = 0;
        bool ordinaryPassword = false;
        QString openingError;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            auto *active = dialogs.activeDialog();
            if (auto *message = qobject_cast<QMessageBox *>(active)) {
                openingError = message->text();
                message->accept();
            }
            if (active && active->objectName() == "passwordDialog") {
                ordinaryPassword = true;
                active->reject();
            }
            if (auto *chooser = qobject_cast<DeviceSelectionDialog *>(active)) {
                auto *tree = chooser->findChild<QTreeWidget *>("deviceItems");
                if (tree->isEnabled() && tree->topLevelItemCount()) {
                    tree->setCurrentItem(tree->topLevelItem(0));
                    chooser->findChild<QPushButton *>("deviceOpenButton")->click();
                }
            }
            if (auto *dialog = qobject_cast<FscryptKeyDialog *>(active)) {
                ++prompts;
                if (mode == "cancel")
                    dialog->reject();
                else if (mode == "none")
                    dialog->findChild<QPushButton *>("fscryptWithoutKeys")->click();
                else {
                    for (int i = 0; i < 2; ++i) {
                        if (i)
                            dialog->findChild<QPushButton *>("fscryptAddKey")->click();
                        const auto item = cases[i ? 4 : 0].toObject();
                        dialog->findChildren<QComboBox *>("fscryptVersion")[i]->setCurrentIndex(
                            i ? 0 : 1);
                        dialog->findChildren<QLineEdit *>("fscryptMasterKey")[i]->setText(
                            item["raw_key"].toString());
                        dialog->findChildren<QLineEdit *>("fscryptDescriptor")[i]->setText(
                            item["descriptor"].toString());
                    }
                    dialog->accept();
                }
            }
        });
        automation.start(10);
        if (device)
            window.findChild<QAction *>("openDeviceAction")->trigger();
        else
            window.openPath(image);
        QTRY_VERIFY_WITH_TIMEOUT(prompts || ordinaryPassword || !openingError.isEmpty(), 15000);
        QVERIFY2(openingError.isEmpty(), qPrintable(openingError));
        QVERIFY(!ordinaryPassword);
        QCOMPARE(prompts, 1u);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        if (mode == "cancel") {
            QVERIFY(Row(*table, QFileInfo(image).fileName()) >= 0);
            return;
        }
        const auto item = cases.first().toObject();
        QTRY_VERIFY(Row(*table, item["name"].toString()) >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            table, "doubleClicked", Qt::DirectConnection,
            Q_ARG(QModelIndex, table->model()->index(Row(*table, item["name"].toString()), 0))));
        const QString name = item[mode == "keys" ? "filename" : "nokey_name"].toString();
        QTRY_VERIFY(Row(*table, name) >= 0);
        window.findChild<QAction *>("refreshAction")->trigger();
        QTRY_VERIFY(Row(*table, name) >= 0);
        QCOMPARE(prompts, 1u);
        // Device copying also verifies the accepted keys reach the data reader.
        // Image cases above cover opening, names and refresh; the native fixture
        // has POSIX colons rejected by the existing Windows image-copy policy.
        if (mode == "keys" && device) {
            table->selectRow(Row(*table, name));
            ExtractionOptions options;
            options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
            window.extractTo(directory.filePath("copied"), true, {}, options);
#ifdef Q_OS_WIN
            const QString outputName = QString(name).replace(':', '_');
#else
            const QString outputName = name;
#endif
            QFile output(
                directory.filePath("copied/" + item["name"].toString() + '/' + outputName));
            QTRY_VERIFY_WITH_TIMEOUT(output.exists() || !openingError.isEmpty(), 15000);
            QVERIFY2(openingError.isEmpty(), qPrintable(openingError));
            QVERIFY(output.exists());
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(QCryptographicHash::hash(output.readAll(), QCryptographicHash::Sha256).toHex(),
                     item["native_sha256"].toString().toLatin1());
        }
        automation.stop();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
#endif
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(FscryptKeyUiTests)
#include "fscrypt_key_ui_tests.moc"
