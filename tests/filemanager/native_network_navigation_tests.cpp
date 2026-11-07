#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/native_network_share.h"
#include "ui/support/address_popup_support.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class NativeNetworkNavigationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

  private slots:
    void serverAddressShareParentAndRefusal_data() {
        QTest::addColumn<QString>("language");
        for (const char *language : {"-", "ru"})
            QTest::newRow(language) << QString::fromLatin1(language);
    }
    void serverAddressShareParentAndRefusal() {
        QFETCH(QString, language);
        SaveRegLang(UString(language.toStdWString().c_str()),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const auto restoreLanguage = qScopeGuard([] {
            SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        });
        NativeNetworkShare fixture;
        const QString fixtureError = fixture.create();
        QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
        qInfo("Owned share %s at %s; server directory=%d canonical=%s", qPrintable(fixture.name()),
              qPrintable(QFileInfo(fixture.filePath()).path()), QFileInfo(fixture.server()).isDir(),
              qPrintable(QFileInfo(fixture.server()).canonicalFilePath()));
        ControlledBackend backend;
        MainWindow window(&backend);
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
            }
        });
        responder.start(1);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(action(window, "refreshAction")->isEnabled(), 5000);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(fixture.server());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, fixture.name()) >= 0, 5000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(QDir::toNativeSeparators(address->text())
                     .compare(fixture.server(), Qt::CaseInsensitive),
                 0);
        table(window)->selectRow(filesystemRow(window, fixture.name()));
        for (const char *name :
             {"copyAction", "moveAction", "deleteAction", "renameAction", "hashSHA256Action"})
            QVERIFY2(!action(window, name)->isEnabled(), name);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*address, window));
            QVERIFY(UiInspection::fullyVisible(*table(window), window));
            QVERIFY(UiInspection::separate(*address, *table(window), window));
            QVERIFY(UiInspection::writeReport(
                window, QString("network-server-%1-%2").arg(language).arg(size.width())));
        }
        openRow(window, filesystemRow(window, fixture.name()));
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, NativeNetworkShare::fileName()) >= 0, 5000);
        QCOMPARE(address->text(), QFileInfo(fixture.sharePath()).canonicalFilePath());
        QCOMPARE(read(fixture.sharePath() + NativeNetworkShare::fileName()),
                 NativeNetworkShare::payload());
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QVERIFY(combo);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        QString serverLabel = fixture.server();
        if (serverLabel.endsWith('\\'))
            serverLabel.chop(1);
        QCOMPARE(
            QDir::toNativeSeparators(combo->itemText(0)).compare(serverLabel, Qt::CaseInsensitive),
            0);
        AddressPopupSupport::selectRow(*combo, 0);
        QVERIFY(!QTest::currentTestFailed());
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, fixture.name()) >= 0, 5000);
        openRow(window, filesystemRow(window, fixture.name()));
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, NativeNetworkShare::fileName()) >= 0, 5000);
        action(window, "parentFolderAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(filesystemRow(window, fixture.name()) >= 0, 5000);
        QCOMPARE(QDir::toNativeSeparators(address->text())
                     .compare(fixture.server(), Qt::CaseInsensitive),
                 0);
        QCOMPARE(table(window)->model()->index(table(window)->currentRow(), 0).data().toString(),
                 fixture.name());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(fixture.remove(), 0ul);
        openRow(window, filesystemRow(window, fixture.name()));
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        QCOMPARE(QDir::toNativeSeparators(address->text())
                     .compare(fixture.server(), Qt::CaseInsensitive),
                 0);
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(filesystemRow(window, fixture.name()), -1, 5000);
        QCOMPARE(read(fixture.filePath()), NativeNetworkShare::payload());
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(backend.extractCalls, 0);
    }
};

QTEST_MAIN(NativeNetworkNavigationTests)
#include "native_network_navigation_tests.moc"
