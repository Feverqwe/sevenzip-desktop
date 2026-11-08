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
#include <QLabel>
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

    void singleNameValidationAndKeyboardAcceptance() {
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
        name->setFocus();
        name->clear();
        QTest::keyClick(name, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(ToQString(dialog.Value), directory);
        name->setText("../outside");
        QVERIFY(!ok->isEnabled());
#ifdef Q_OS_WIN
        name->setText(" ... ");
        QVERIFY(!ok->isEnabled());
#endif
        auto *destination = dialog.findChild<QLineEdit *>("copyDestination");
        destination->setText(source);
        QVERIFY(!ok->isEnabled());
        destination->setText(directory);
        name->setText(QStringLiteral(" копия δ "));
        QVERIFY(ok->isEnabled());
        name->setFocus();
        QTest::keyClick(name, Qt::Key_Return);
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
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
#ifdef Q_OS_WIN
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        for (const auto *invalid : {" ... ", " ... /", " ... /copy.txt", " ... /../copy.txt"}) {
            path->setText(fixture.filePath(invalid));
            QVERIFY(!ok->isEnabled());
            dialog.accept();
            QVERIFY(dialog.isVisible());
        }
#endif
        path->setText(saved);
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

    void destinationNameWithWrongTypeCanBeCorrected_data() {
        QTest::addColumn<bool>("move");
        QTest::newRow("copy-file") << false;
        QTest::newRow("move-folder") << true;
    }
    void destinationNameWithWrongTypeCanBeCorrected() {
        QFETCH(bool, move);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("item");
        const QString folder = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(folder));
        const QString occupied = QDir(folder).filePath("item");
        QVERIFY(QDir().mkdir(move ? source : occupied));
        QFile file(move ? occupied : source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CCopyDialog dialog(move, folder + '/', {source});
        QtCopyDestinationContext context;
        context.SourceDirectory = fixture.path();
        context.SourceName = "item";
        context.LocalFileTransfer = true;
        context.CanRename = [](const QString &) { return true; };
        dialog.ConfigureDestination(context);
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(name->isVisible());
        QVERIFY(!ok->isEnabled());
        dialog.accept();
        QVERIFY(dialog.isVisible());
        name->setText("other name");
        QVERIFY(ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), QDir(folder).filePath("other name"));
    }

    void linkedDestinationNameCanBeCorrected() {
#ifndef Q_OS_UNIX
        QSKIP("The symbolic link fixture requires POSIX");
#else
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString folder = fixture.filePath("destination");
        QVERIFY(QDir().mkdir(folder));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString linked = QDir(folder).filePath("source.txt");
        const QString dangling = QDir(folder).filePath("dangling.txt");
        QFile protectedFile(fixture.filePath("protected.txt"));
        QVERIFY(protectedFile.open(QIODevice::WriteOnly));
        protectedFile.close();
        QVERIFY(QFile::link(protectedFile.fileName(), linked));
        QVERIFY(QFile::link(fixture.filePath("missing.txt"), dangling));

        CCopyDialog dialog(false, folder + '/', {source});
        QtCopyDestinationContext context;
        context.SourceDirectory = fixture.path();
        context.SourceName = "source.txt";
        context.LocalFileTransfer = true;
        context.CanRename = [](const QString &) { return true; };
        dialog.ConfigureDestination(context);
        dialog.show();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled());
        path->setText(dangling);
        QVERIFY(!ok->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>("copyDestinationMessage")->text(),
                 QString("Destination is a symbolic link: %1").arg(dangling));
        path->setText(folder + '/');
        name->setText("dangling.txt");
        QVERIFY(!ok->isEnabled());
        dialog.accept();
        QVERIFY(dialog.isVisible());
        name->setText("other.txt");
        QVERIFY(ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), QDir(folder).filePath("other.txt"));
#endif
    }

    void batchDestinationConflictCanBeCorrected() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first.txt");
        const QString second = fixture.filePath("second.txt");
        for (const auto &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString blocked = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(QDir(blocked).filePath("second.txt")));
        CCopyDialog dialog(true, blocked + '/', {first, second});
        QtCopyDestinationContext context;
        context.SourceDirectory = fixture.path();
        context.LocalFileTransfer = true;
        dialog.ConfigureDestination(context);
        dialog.show();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        ok->setFocus();
        QTest::keyClick(ok, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.findChild<QLabel *>("copyDestinationMessage")->isVisible());
        QCOMPARE(dialog.focusWidget(), path);
        QVERIFY(!QFileInfo::exists(QDir(blocked).filePath("first.txt")));

        const QString corrected = fixture.filePath("different folder") + '/';
        path->setText(corrected);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), corrected);
    }

    void destinationBelowReadOnlyFolderCanBeCorrected() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString blocked = fixture.filePath("read only");
        QVERIFY(QDir().mkdir(blocked));
        const auto permissions = QFile::permissions(blocked);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(blocked, permissions); });
        if (!QFile::setPermissions(blocked, QFile::ReadOwner | QFile::ExeOwner) ||
            QFileInfo(blocked).isWritable())
            QSKIP("The test filesystem does not enforce this directory permission");

        const QString source = fixture.filePath("source.txt");
        const QString target = fixture.filePath("writable target.txt");
        CCopyDialog dialog(false, blocked + "/new folder/target.txt", {source});
        configure(dialog, source);
        dialog.show();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        for (const QString &unwritable :
             {blocked + "/new folder/target.txt", blocked + "/new folder/deeper/"}) {
            path->setText(unwritable);
            QVERIFY(!ok->isEnabled());
            dialog.accept();
            QVERIFY(dialog.isVisible());
            QCOMPARE(path->text(), unwritable);
        }
        path->setText(target);
        QVERIFY(ok->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), target);
    }

    void browsePreservesNameDraft_data() {
        QTest::addColumn<bool>("invalidDraft");
        QTest::newRow("valid-path") << false;
        QTest::newRow("invalid-path") << true;
    }

    void browsePreservesNameDraft() {
        QFETCH(bool, invalidDraft);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString selected = fixture.filePath("selected folder");
        QVERIFY(QDir().mkpath(selected));
        CCopyDialog dialog(false, fixture.path() + '/', {fixture.filePath("source.txt")});
        configure(dialog, fixture.filePath("source.txt"));
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        name->setText("draft.txt");
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        if (invalidDraft)
            path->setText(path->text() + QChar::Null);
        const QString destination = path->text();
        for (bool accept : {false, true}) {
            bool found = false;
            QTimer::singleShot(0, &dialog, [&] {
                auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (!picker)
                    return;
                found = true;
                const auto close = qScopeGuard([&] {
                    if (picker->isVisible())
                        picker->reject();
                });
                if (accept) {
                    picker->setDirectory(selected);
                    picker->selectFile(selected);
                    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
                }
            });
            dialog.findChild<QPushButton *>("copyBrowseButton")->click();
            QVERIFY(found);
            QCOMPARE(name->text(), QString("draft.txt"));
            if (accept)
                QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(path->text())), selected);
            else
                QCOMPARE(path->text(), destination);
        }
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), fixture.path() + '/');
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CopyDestinationTests)
#include "copy_destination_tests.moc"
