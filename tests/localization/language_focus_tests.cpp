#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/translation_catalog.h"
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class LanguageFocusTests : public QObject {
    Q_OBJECT
    QString settingsPath() const { return qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"); }

  private slots:
    void init() {
        QFile::remove(settingsPath());
        SaveRegLang(UString("-"), settingsPath());
        ReloadLang(settingsPath());
    }
    void cleanup() {
        SaveRegLang(UString("-"), settingsPath());
        ReloadLang(settingsPath());
    }
    void russianShowPasswordRetainsKeyboardReachability() {
        QTemporaryDir directory;
        SaveRegLang(UString("ru"), settingsPath());
        ReloadLang(settingsPath());
        CreateArchiveDialog dialog(directory.path());
        auto *show = dialog.findChild<QCheckBox *>("archiveShowPassword");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        QVERIFY(show && password);
        QCOMPARE(show->text(), QStringLiteral("&Показать пароль"));
        password->setText(QStringLiteral("pending secret 密碼"));
        dialog.show();
        dialog.activateWindow();
        QTRY_VERIFY(dialog.isActiveWindow());
        QSignalSpy toggles(show, &QCheckBox::toggled);
        password->setFocus();
        const QKeySequence mnemonic = QKeySequence::mnemonic(show->text());
        QCOMPARE(show->shortcut(), mnemonic);
        if (!mnemonic.isEmpty()) {
            const auto key = mnemonic[0];
            QTest::sendKeyEvent(QTest::Click, dialog.windowHandle(), key.key(),
                                QString(QChar(ushort(key.key()))), key.keyboardModifiers());
            QTRY_COMPARE(toggles.count(), 1);
        }
        for (int i = 0; i < 80 && dialog.focusWidget() != show; ++i)
            QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab);
        QCOMPARE(dialog.focusWidget(), show);
        QCOMPARE(password->text(), QStringLiteral("pending secret 密碼"));
    }
    void compositeRowLabelsFocusControls() {
        // Composite form rows must direct their label association to the input.
        QTemporaryDir directory;
        CreateArchiveDialog dialog(directory.path());
        dialog.show();
        dialog.activateWindow();
        QTRY_VERIFY(dialog.isActiveWindow());
        for (const char *lang : {"-", "ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            for (const char *name : {"compressionThreads", "compressionMemoryLimit"}) {
                auto *combo = dialog.findChild<QComboBox *>(name);
                QVERIFY(combo && combo->isEnabled());
                QLabel *label = nullptr;
                for (auto *candidate : dialog.findChildren<QLabel *>())
                    if (candidate->buddy() == combo->parentWidget())
                        label = candidate;
                QVERIFY(label && !combo->accessibleName().isEmpty());
                QCOMPARE(combo->accessibleName(), QString(label->text()).remove('&'));
                dialog.findChild<QLineEdit *>("archiveName")->setFocus();
                label->buddy()->setFocus();
                QCOMPARE(dialog.focusWidget(), combo);
            }
        }
    }
    void actualFormLanguageFallbacksRetainPendingValues() {
        QTemporaryDir directory;
        CreateArchiveDialog dialog(directory.path());
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        level->setCurrentIndex(level->count() - 1);
        const QString englishLevel = level->currentText();
        const QVariant levelData = level->currentData();
        name->setText(QStringLiteral("pending name 密碼.7z"));
        password->setText(QStringLiteral("pending secret 密碼"));
        SaveRegLang(UString("ru"), settingsPath());
        ReloadLang(settingsPath());
        QtNotifyLanguageChanged();
        QVERIFY(level->currentText() != englishLevel);
        QVERIFY(WriteTranslationCatalog(directory.path(), {{4000, "Partial Add"}}));
        QFile partial(directory.filePath("sevenzip_de.qm"));
        SaveRegLang(UString("de"), settingsPath());
        ReloadLang(settingsPath(), directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(dialog.windowTitle(), QString("Partial Add"));
        QCOMPARE(level->currentText(), englishLevel);
        QVERIFY(WriteTranslationCatalog(directory.path(), {{4000, {}}}));
        ReloadLang(settingsPath(), directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(dialog.windowTitle(), QString("Add to Archive"));
        QCOMPARE(level->currentText(), englishLevel);
        QVERIFY(partial.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(partial.write("malformed language"), qint64(18));
        partial.close();
        ReloadLang(settingsPath(), directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(dialog.windowTitle(), QString("Add to Archive"));
        QCOMPARE(level->currentText(), englishLevel);
        QCOMPARE(level->currentData(), levelData);
        QCOMPARE(name->text(), QStringLiteral("pending name 密碼.7z"));
        QCOMPARE(password->text(), QStringLiteral("pending secret 密碼"));
    }
};
QTEST_MAIN(LanguageFocusTests)
#include "language_focus_tests.moc"
