#include "dialogs/copy/CopyDialog.h"
#include "settings/ViewSettings.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
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
        CCopyDialog dialog(false, directory, {source});
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

    void typedPathKeepsDirectValue() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString saved = fixture.filePath(QStringLiteral("old copy δ.txt"));
        CCopyDialog dialog(false, fixture.path() + '/', {source});
        configure(dialog, source);
        dialog.show();
        dialog.findChild<QLineEdit *>("copyDestination")->setText(saved);
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        QVERIFY(name && !name->isVisible());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), saved);
    }

    void peerRouteKeepsIdentity() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString peer = fixture.filePath("archive.zip/nested/");
        CCopyDialog dialog(false, fixture.path() + '/', {source});
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
        const QString archive = fixture.filePath("readonly.zip");
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString peer = archive + '/';
        CCopyDialog dialog(false, peer, {fixture.filePath("source")});
        QtCopyDestinationContext context{fixture.path(), "source", peer, false, {}};
        context.PeerIsArchive = true;
        dialog.ConfigureDestination(context);
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        dialog.findChild<QLineEdit *>("copyDestination")->setText(archive);
        QVERIFY(ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), archive);
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
        CCopyDialog panelDialog(false, "target", {source});
        QtCopyDestinationContext context{
            sourceDirectory, "source.txt", {}, false, [](const QString &) { return true; }};
        context.RelativeBase = sourceDirectory + '/';
        panelDialog.ConfigureDestination(context);
        panelDialog.show();
        auto *name = panelDialog.findChild<QLineEdit *>("copyDestinationName");
        QVERIFY(name && name->isVisible());
        name->setText("renamed.txt");
        QString browsedDirectory;
        QTimer::singleShot(0, &panelDialog, [&] {
            for (auto *picker : panelDialog.findChildren<QFileDialog *>())
                if (picker->isVisible()) {
                    browsedDirectory = picker->directory().canonicalPath();
                    picker->reject();
                }
        });
        panelDialog.findChild<QPushButton *>("copyBrowseButton")->click();
        QCOMPARE(browsedDirectory, QFileInfo(sourceDirectory + "/target").canonicalFilePath());
        QCOMPARE(panelDialog.findChild<QLineEdit *>("copyDestination")->text(), QString("target"));
        QCOMPARE(name->text(), QString("renamed.txt"));
        panelDialog.accept();
        QCOMPARE(panelDialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panelDialog.Value)),
                 QString("target/renamed.txt"));

        // Save Copy keeps its process-relative contract for the same raw input.
        context.RelativeBase = QString();
        CCopyDialog processDialog(false, "target", {source});
        processDialog.ConfigureDestination(context);
        processDialog.show();
        QVERIFY(!processDialog.findChild<QLineEdit *>("copyDestinationName")->isVisible());
        processDialog.accept();
        QCOMPARE(processDialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(processDialog.Value), QString("target"));
    }

    void noSelectionCannotStart() {
        CCopyDialog dialog(false, "/destination/", {});
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(ToQString(dialog.Value), QString("/destination/"));
    }

    void destinationBelowAFileCanBeCorrected() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString blocker = fixture.filePath("existing file");
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString source = fixture.filePath("source.txt");
        const QString blocked = blocker + "/nested/target.txt";
        const QString corrected = fixture.filePath("new folder/target.txt");
        CCopyDialog dialog(false, blocked, {source});
        configure(dialog, source);
        dialog.show();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled());
        dialog.accept();
        QVERIFY(dialog.isVisible());
        QCOMPARE(path->text(), blocked);

        path->setText(blocker);
        QVERIFY(ok->isEnabled()); // An existing leaf file may be overwritten.
        path->setText(corrected);
        QVERIFY(ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), corrected);

        CCopyDialog multiple(true, blocker, {source, fixture.filePath("second.txt")});
        configure(multiple, source);
        multiple.show();
        auto *move =
            multiple.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(!move->isEnabled());
        multiple.findChild<QLineEdit *>("copyDestination")->setText(fixture.filePath("new folder"));
        QVERIFY(move->isEnabled());
    }

    void browseCancelKeepsNameDraft() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CCopyDialog dialog(false, fixture.path() + '/', {fixture.filePath("source.txt")});
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
