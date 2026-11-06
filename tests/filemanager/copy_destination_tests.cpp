#include "dialogs/copy/CopyDialog.h"
#include "settings/ViewSettings.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

class CopyDestinationTests final : public QObject {
    Q_OBJECT

    static void configure(CCopyDialog &dialog, const QString &source) {
        const QFileInfo file(source);
        dialog.ConfigureDestination(
            {file.absolutePath(), file.fileName(), {}, false, [](const QString &) {
                 return true;
             }});
    }

  private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        UiInspection::configureReviewAppearance();
    }

    void singleNameCancelAndAcceptance() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("source δ.txt"));
        const QString directory = fixture.path() + '/';
        CCopyDialog dialog(false, directory, {source}, {});
        configure(dialog, source);
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(name && name->isVisible());
        QVERIFY(!ok->isEnabled()); // The initial proposal would copy onto itself.
        name->clear();
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(ToQString(dialog.Value), directory);
        name->setText("../outside");
        QVERIFY(!ok->isEnabled());
        auto *destination = dialog.findChild<QLineEdit *>("copyDestination");
        destination->setText(source);
        QVERIFY(!ok->isEnabled());
        destination->setText(directory);
        name->setText(QStringLiteral(" копия δ "));
        QVERIFY(ok->isEnabled());
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), directory);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(ToQString(dialog.Value)),
                 fixture.filePath(QStringLiteral(" копия δ ")));
    }

    void historyKeepsDirectValue() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString saved = fixture.filePath(QStringLiteral("old copy δ.txt"));
        CCopyDialog dialog(false, fixture.path() + '/', {source}, {saved});
        configure(dialog, source);
        dialog.show();
        dialog.findChild<QPushButton *>("copyRecentDestinations")
            ->menu()
            ->actions()
            .first()
            ->trigger();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        QVERIFY(name && !name->isVisible());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), saved);
    }

    void historyKeyboardChangesOnlyTheDraft() {
        const QString initial = QStringLiteral("/initial/");
        const QString chosen = QStringLiteral(" /папка & 空/old copy.txt ");
        CCopyDialog dialog(false, initial, {"/source/file.txt"}, {"/first/", chosen});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *recent = dialog.findChild<QPushButton *>("copyRecentDestinations");
        auto *menu = recent->menu();
        bool answered = false;
        QTimer::singleShot(0, &dialog, [&] {
            QVERIFY(menu->isVisible());
            QVERIFY(UiInspection::writeReport(*menu, "copy-recent-destinations"));
            menu->setActiveAction(menu->actions().first());
            QTest::keyClick(menu, Qt::Key_Down);
            QTest::keyClick(menu, Qt::Key_Return);
            answered = true;
        });
        QTest::mouseClick(recent, Qt::LeftButton);
        QTRY_VERIFY(answered);
        QCOMPARE(path->text(), chosen);
        QCOMPARE(ToQString(dialog.Value), initial);
        QVERIFY(dialog.isVisible());
        QVERIFY(WindowActivation::waitForActive(dialog));
        QTRY_VERIFY(path->hasFocus());
        QCOMPARE(path->selectedText(), chosen);
        QVERIFY(UiInspection::writeReport(dialog, "copy-recent-destination-chosen"));
        answered = false;
        QTimer::singleShot(0, &dialog, [&] {
            QVERIFY(menu->isVisible());
            QTest::keyClick(menu, Qt::Key_Escape);
            answered = true;
        });
        QTest::mouseClick(recent, Qt::LeftButton);
        QTRY_VERIFY(answered);
        QVERIFY(dialog.isVisible());
        QCOMPARE(path->text(), chosen);
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), initial);
    }

    void peerRouteKeepsIdentity() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString peer = fixture.filePath("archive.zip/nested/");
        CCopyDialog dialog(false, fixture.path() + '/', {source}, {});
        dialog.ConfigureDestination({fixture.path(), "source.txt", peer, true,
                                     [peer](const QString &path) { return path != peer; }});
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        name->setText("draft.txt");
        auto *button = dialog.findChild<QPushButton *>("copyPeerDestination");
        QVERIFY(button && button->isVisible() && button->isEnabled());
        button->click();
        QVERIFY(!name->isVisible());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), peer);
    }

    void unavailablePeerCannotStart() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString peer = fixture.filePath("readonly.zip/");
        CCopyDialog dialog(false, peer, {fixture.filePath("source")}, {});
        dialog.ConfigureDestination({fixture.path(), "source", peer, false, {}});
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void relativeDestinationUsesItsCallerBase() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString sourceDirectory = fixture.filePath("source");
        const QString processDirectory = fixture.filePath("process");
        QVERIFY(QDir().mkpath(sourceDirectory + "/target"));
        QVERIFY(QDir().mkpath(processDirectory));
        const QString previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(processDirectory));
        const QString source = sourceDirectory + "/source.txt";
        CCopyDialog panelDialog(false, "target", {source}, {});
        QtCopyDestinationContext context{
            sourceDirectory, "source.txt", {}, false, [](const QString &) { return true; }};
        context.RelativeBase = sourceDirectory + '/';
        panelDialog.ConfigureDestination(context);
        panelDialog.show();
        auto *name = panelDialog.findChild<QLineEdit *>("copyDestinationName");
        QVERIFY(name && name->isVisible());
        name->setText("renamed.txt");
        panelDialog.accept();
        QCOMPARE(panelDialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panelDialog.Value)),
                 QString("target/renamed.txt"));

        // Save Copy keeps its process-relative contract for the same raw input.
        context.RelativeBase = QString();
        CCopyDialog processDialog(false, "target", {source}, {});
        processDialog.ConfigureDestination(context);
        processDialog.show();
        QVERIFY(!processDialog.findChild<QLineEdit *>("copyDestinationName")->isVisible());
        processDialog.accept();
        QCOMPARE(processDialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(processDialog.Value), QString("target"));
    }

    void noSelectionCannotStart() {
        CCopyDialog dialog(false, "/destination/", {}, {});
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(ToQString(dialog.Value), QString("/destination/"));
    }

    void browseCancelKeepsNameDraft() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CCopyDialog dialog(false, fixture.path() + '/', {fixture.filePath("source.txt")}, {});
        configure(dialog, fixture.filePath("source.txt"));
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        name->setText("draft.txt");
        const QString destination = dialog.findChild<QLineEdit *>("copyDestination")->text();
        bool found = false;
        QTimer::singleShot(0, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *picker = qobject_cast<QFileDialog *>(widget);
                    picker && picker->isVisible()) {
                    found = true;
                    picker->reject();
                }
        });
        dialog.findChild<QPushButton *>("copyBrowseButton")->click();
        QVERIFY(found);
        QCOMPARE(name->text(), QString("draft.txt"));
        QCOMPARE(dialog.findChild<QLineEdit *>("copyDestination")->text(), destination);
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), fixture.path() + '/');
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CopyDestinationTests)
#include "copy_destination_tests.moc"
