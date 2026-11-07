#include "dialogs/combo/ComboDialog.h"
#include "dialogs/copy/CopyDialog.h"
#include "dialogs/extract/alias.h"
#include "dialogs/mem/MemDialog.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "settings/RegistryUtils.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <memory>
#ifdef Q_OS_MACOS
#include "support/native_sheet.h"
#endif

class ContextDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray previous_;
    bool hadSettings_ = false;

    static bool nativeSheet(QWidget &dialog, QWidget &parent) {
#ifdef Q_OS_MACOS
        return QApplication::platformName() != "cocoa" || NativeSheetAttachedTo(dialog, parent);
#else
        Q_UNUSED(dialog);
        Q_UNUSED(parent);
        return true;
#endif
    }

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        hadSettings_ = qEnvironmentVariableIsSet("SEVENZIP_SETTINGS_PATH");
        previous_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("context.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (hadSettings_)
            qputenv("SEVENZIP_SETTINGS_PATH", previous_);
        else
            qunsetenv("SEVENZIP_SETTINGS_PATH");
    }
    void contextualTaskBlocksItsOwner_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"name", "copy", "password", "memory", "overwrite", "extract"})
            QTest::newRow(kind) << QString(kind);
    }
    void contextualTaskBlocksItsOwner() {
        QFETCH(QString, kind);
        QWidget owner, peer;
        owner.setWindowTitle("Task owner");
        peer.setWindowTitle("Independent window");
        auto *layout = new QVBoxLayout(&owner);
        auto *origin = new QLineEdit("owner draft", &owner);
        auto *blocked = new QPushButton("Owner action", &owner);
        layout->addWidget(origin);
        layout->addWidget(blocked);
        auto *peerLayout = new QVBoxLayout(&peer);
        auto *available = new QPushButton("Independent action", &peer);
        peerLayout->addWidget(available);
        owner.setGeometry(660, 100, 600, 420);
        peer.setGeometry(20, 100, 400, 320);
        peer.show();
        owner.show();
        QVERIFY(WindowActivation::waitForActive(owner));
        origin->setFocus();
        std::unique_ptr<QDialog> dialog;
        if (kind == "name")
            dialog = std::make_unique<CComboDialog>("Create", "Name", "initial", &owner);
        else if (kind == "copy")
            dialog = std::make_unique<CCopyDialog>(false, settings_.path(), QStringList{"fixture"},
                                                   &owner);
        else if (kind == "password") {
            auto prompt = std::make_unique<CPasswordDialog>(&owner);
            prompt->OnInit();
            dialog = std::move(prompt);
        } else if (kind == "memory") {
            auto prompt = std::make_unique<CMemDialog>(&owner);
            prompt->OnInit();
            dialog = std::move(prompt);
        } else if (kind == "overwrite") {
            auto prompt = std::make_unique<COverwriteDialog>(&owner);
            prompt->OnInit();
            dialog = std::move(prompt);
        } else
            dialog = std::make_unique<CExtractDialog>(settings_.path(), QStringList{}, &owner);
        int ownerClicks = 0, peerClicks = 0;
        connect(blocked, &QPushButton::clicked, &owner, [&] { ++ownerClicks; });
        connect(available, &QPushButton::clicked, &peer, [&] { ++peerClicks; });
        bool observed = false;
        QTimer::singleShot(0, dialog.get(), [&] {
            const auto close = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
            QTRY_VERIFY(nativeSheet(*dialog, owner));
            QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                              blocked->mapTo(&owner, blocked->rect().center()));
            QTest::mouseClick(peer.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                              available->mapTo(&peer, available->rect().center()));
            QCoreApplication::processEvents();
            QCOMPARE(ownerClicks, 0);
#ifdef Q_OS_MACOS
            QCOMPARE(peerClicks, 1);
#else
            QCOMPARE(peerClicks, 0);
#endif
            QVERIFY(UiInspection::writeReport(*dialog, "context-task-" + kind));
            observed = true;
        });
        const int cancelled =
            kind == "memory" || kind == "overwrite" ? IDCANCEL : int(QDialog::Rejected);
        QCOMPARE(dialog->exec(), cancelled);
        QVERIFY(observed);
        QVERIFY(WindowActivation::waitForActive(owner));
        QTRY_COMPARE(owner.focusWidget(), origin);
        QCOMPARE(origin->text(), QString("owner draft"));
    }
    void nestedPromptCancelKeepsParentDraft() {
        QWidget owner;
        owner.resize(800, 500);
        owner.show();
        QVERIFY(WindowActivation::waitForActive(owner));
        CComboDialog outer("Create", "Name", "initial", &owner);
        auto *value = outer.findChild<QComboBox *>("comboValue");
        bool observed = false;
        QTimer::singleShot(0, &outer, [&] {
            const auto close = qScopeGuard([&] {
                if (outer.isVisible())
                    outer.reject();
            });
            QVERIFY(QTest::qWaitForWindowExposed(&outer));
            QTRY_VERIFY(nativeSheet(outer, owner));
            const QString raw = QStringLiteral("  черновик 密碼  ");
            value->setEditText(raw);
            CPasswordDialog inner(&outer);
            inner.Password = L"original";
            inner.OnInit();
            bool innerObserved = false;
            QTimer::singleShot(0, &inner, [&] {
                const auto cancel = qScopeGuard([&] { inner.reject(); });
                QVERIFY(QTest::qWaitForWindowExposed(&inner));
                QTRY_VERIFY(nativeSheet(inner, outer));
                inner.findChild<QLineEdit *>("passwordEdit")->setText("discarded");
                QTest::keyClick(&inner, Qt::Key_Escape);
                innerObserved = true;
            });
            QCOMPARE(inner.exec(), int(QDialog::Rejected));
            QVERIFY(innerObserved);
            QCOMPARE(QString::fromWCharArray(inner.Password), QString("original"));
            QVERIFY(outer.isVisible());
            QCOMPARE(value->currentText(), raw);
            QTest::keyClick(value, Qt::Key_Return);
            QCOMPARE(QString::fromWCharArray(outer.Value), raw);
            observed = true;
        });
        QCOMPARE(outer.exec(), int(QDialog::Accepted));
        QVERIFY(observed);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ContextDialogTests)
#include "context_dialog_tests.moc"
