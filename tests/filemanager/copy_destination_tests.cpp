#include "dialogs/copy/CopyDialog.h"
#include "settings/ViewSettings.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QStorageInfo>
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
        name->setText("copy?.txt");
        QVERIFY(!ok->isEnabled());
        QTest::keyClick(name, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
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
        for (const auto *invalid : {" ... ", " ... /", " ... /copy.txt", " ... /../copy.txt",
                                    "wrong?/copy.txt", "copy*.txt"}) {
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

#ifdef Q_OS_WIN
    void unavailableDestinationDriveCanBeCorrected() {
        QString root;
        for (char16_t letter = u'Z'; letter >= u'D'; --letter) {
            const QString candidate = QString(QChar(letter)) + ":/";
            if (!QDir(candidate).exists()) {
                root = candidate;
                break;
            }
        }
        if (root.isEmpty())
            QSKIP("No unavailable drive letter");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        for (bool move : {false, true}) {
            const QString suffix = move ? "new folder/" : "new folder/target.txt";
            const QString blocked = root + suffix;
            CCopyDialog dialog(move, blocked, {source});
            configure(dialog, source);
            dialog.show();
            auto *path = dialog.findChild<QLineEdit *>("copyDestination");
            auto *ok =
                dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
            QVERIFY(!ok->isEnabled());
            dialog.accept();
            QVERIFY(dialog.isVisible());
            QCOMPARE(path->text(), blocked);
            QVERIFY(dialog.findChild<QLabel *>("copyDestinationMessage")->text().contains(root));
            const QString corrected = fixture.filePath(suffix);
            path->setText(corrected);
            QVERIFY(ok->isEnabled());
            dialog.accept();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(ToQString(dialog.Value), corrected);
        }
    }
#endif

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
        QCOMPARE(dialog.findChild<QLabel *>("copyDestinationMessage")->text(),
                 QString("Not a folder: %1").arg(blocker));

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
        QTest::addColumn<bool>("removeConflict");
        QTest::newRow("copy-file") << false << false;
        QTest::newRow("move-folder") << true << false;
        QTest::newRow("retry-same-copy-name") << false << true;
        QTest::newRow("retry-same-move-name") << true << true;
    }
    void destinationNameWithWrongTypeCanBeCorrected() {
        QFETCH(bool, move);
        QFETCH(bool, removeConflict);
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
        const QString lateConflict = QDir(folder).filePath("other name");
        if (move) {
            QFile conflict(lateConflict);
            QVERIFY(conflict.open(QIODevice::WriteOnly));
        } else {
            QVERIFY(QDir().mkdir(lateConflict));
        }
        ok->click();
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.focusWidget(), name);
        QCOMPARE(name->selectedText(), QString("other name"));
        if (removeConflict) {
            QVERIFY(ok->isEnabled());
            ok->click();
            QVERIFY(dialog.isVisible());
            QVERIFY(move ? QFile::remove(lateConflict) : QDir().rmdir(lateConflict));
        } else
            QTest::keyClicks(name, "corrected name");
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value),
                 removeConflict ? lateConflict : QDir(folder).filePath("corrected name"));
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

    void duplicateBatchNamesKeepTheDestinationDialogOpen() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first/item.txt");
        const QString second = fixture.filePath("second/item.txt");
        for (const auto &source : {first, second}) {
            QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString destination = fixture.filePath("new folder");
        CCopyDialog dialog(false, destination, {first, second});
        QtCopyDestinationContext context;
        context.LocalFileTransfer = true;
        dialog.ConfigureDestination(context);
        dialog.show();
        dialog.accept();
        QVERIFY(dialog.isVisible());
        const auto *error = dialog.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(error && error->isVisible());
        QVERIFY(error->text().contains(QDir(destination).filePath("item.txt")));
        QVERIFY(!QFileInfo::exists(destination));
    }

#ifdef Q_OS_LINUX
    void crossFilesystemMoveWaitsForReadAccess() {
        QTemporaryDir sourceDirectory;
        QTemporaryDir targetDirectory("/dev/shm/sevenzip-copy-test-XXXXXX");
        QVERIFY(sourceDirectory.isValid());
        if (!targetDirectory.isValid() || QStorageInfo(sourceDirectory.path()).device() ==
                                              QStorageInfo(targetDirectory.path()).device())
            QSKIP("Requires two writable filesystems");
        const QString source = sourceDirectory.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const auto permissions = QFile::permissions(source);
        QVERIFY(QFile::setPermissions(source, QFile::WriteOwner));
        if (QFileInfo(source).isReadable())
            QSKIP("The current user can still read the file");
        const QString target = targetDirectory.filePath("new folder/target.txt");
        CCopyDialog move(true, target, {source});
        QtCopyDestinationContext context;
        context.LocalFileTransfer = true;
        move.ConfigureDestination(context);
        move.show();
        move.accept();
        QVERIFY(move.isVisible());
        auto *message = move.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(message && message->isVisible() && message->text().contains(source));
        QVERIFY(QFile::setPermissions(source, permissions));
        QTest::keyClick(move.findChild<QLineEdit *>("copyDestination"), Qt::Key_Return);
        QCOMPARE(move.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(move.Value), target);
    }
#endif

    void mergingMoveWaitsForDirectoryReadAccess() {
#ifndef Q_OS_UNIX
        QSKIP("Requires POSIX directory permissions");
#endif
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString destination = fixture.filePath("destination") + '/';
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(destination + "source"));
        const auto permissions = QFile::permissions(source);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(source, permissions); });
        QVERIFY(QFile::setPermissions(source, QFile::WriteOwner | QFile::ExeOwner));
        if (QFileInfo(source).isReadable())
            QSKIP("The current user can still read the directory");

        CCopyDialog move(true, destination, {source});
        QtCopyDestinationContext context;
        context.LocalFileTransfer = true;
        move.ConfigureDestination(context);
        move.show();
        move.accept();
        QVERIFY(move.isVisible());
        auto *message = move.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(message && message->isVisible() && message->text().contains(source));
        QVERIFY(QDir().rmdir(destination + "source"));
        auto *path = move.findChild<QLineEdit *>("copyDestination");
        QCOMPARE(path->text(), destination);
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(move.result(), int(QDialog::Accepted));
    }

    void copyBatchCanBeRetriedAfterReadAccessReturns() {
#ifndef Q_OS_UNIX
        QSKIP("Requires POSIX file permissions");
#endif
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first.txt");
        const QString blocked = fixture.filePath(QStringLiteral("закрытый файл.txt"));
        for (const auto &path : {first, blocked}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString destination = fixture.filePath("output") + '/';
        QVERIFY(QDir().mkpath(destination));
        const auto permissions = QFile::permissions(blocked);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(blocked, permissions); });
        QVERIFY(QFile::setPermissions(blocked, QFile::WriteOwner));
        if (QFileInfo(blocked).isReadable())
            QSKIP("The current user can still read the file");
        QtCopyDestinationContext context;
        context.SourceDirectory = fixture.path();
        context.LocalFileTransfer = true;
        CCopyDialog copy(false, destination, {first, blocked});
        copy.ConfigureDestination(context);
        copy.show();
        copy.accept();
        QVERIFY(copy.isVisible());
        auto *message = copy.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(message && message->isVisible() && message->text().contains(blocked));

        CCopyDialog move(true, destination, {first, blocked});
        move.ConfigureDestination(context);
        move.accept();
        QCOMPARE(move.result(), int(QDialog::Accepted));

        const QString archive = fixture.filePath("output.zip") + '/';
        context.PeerDestination = archive;
        context.PeerIsArchive = true;
        context.PeerSupportsWriting = true;
        CCopyDialog moveToArchive(true, archive, {first, blocked});
        moveToArchive.ConfigureDestination(context);
        moveToArchive.show();
        moveToArchive.accept();
        QVERIFY(moveToArchive.isVisible());
        auto *archiveMessage = moveToArchive.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(archiveMessage && archiveMessage->isVisible() &&
                archiveMessage->text().contains(blocked));

        QVERIFY(QFile::setPermissions(blocked, permissions));
        auto *archivePath = moveToArchive.findChild<QLineEdit *>("copyDestination");
        QCOMPARE(archivePath->text(), archive);
        QTest::keyClick(archivePath, Qt::Key_Return);
        QCOMPARE(moveToArchive.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(moveToArchive.Value), archive);
        auto *path = copy.findChild<QLineEdit *>("copyDestination");
        QCOMPARE(path->text(), destination);
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(copy.result(), int(QDialog::Accepted));
    }

    void batchDestinationConflictCanBeCorrected_data() {
        QTest::addColumn<bool>("replacePath");
        QTest::newRow("same-destination") << false;
        QTest::newRow("replace-destination") << true;
    }

    void batchDestinationConflictCanBeCorrected() {
        QFETCH(bool, replacePath);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first.txt");
        const QString secondName = QString(110, u'w') + ".txt";
        const QString second = fixture.filePath(secondName);
        for (const auto &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString blocked = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(QDir(blocked).filePath(secondName)));
        CCopyDialog dialog(true, blocked + '/', {first, second});
        QtCopyDestinationContext context;
        context.SourceDirectory = fixture.path();
        context.LocalFileTransfer = true;
        dialog.ConfigureDestination(context);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QApplication::processEvents();
        dialog.resize(360, 240);
        auto *disclosure = dialog.findChild<QWidget *>("copySelectionDisclosure");
        QVERIFY(disclosure);
        auto *details = disclosure->findChild<QToolButton *>();
        QVERIFY(details);
        details->setChecked(true);
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        QApplication::processEvents();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        ok->setFocus();
        scroll->verticalScrollBar()->setValue(0);
        QTest::keyClick(ok, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
        auto *message = dialog.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(message);
        const auto geometry = [&] {
            QString details;
            QDebug out(&details);
            out << "viewport" << scroll->viewport()->size() << "scroll"
                << scroll->verticalScrollBar()->value() << "path"
                << path->mapTo(scroll->viewport(), QPoint()) << path->size() << "message"
                << message->mapTo(scroll->viewport(), QPoint()) << message->size();
            return details;
        };
        QTRY_VERIFY2(UiInspection::fullyVisible(*message, dialog), qPrintable(geometry()));
        QVERIFY(UiInspection::labelFits(*message));
        QVERIFY(UiInspection::fullyVisible(*path, dialog));
        QVERIFY(UiInspection::fullyVisible(*ok, dialog));
        QVERIFY(UiInspection::writeReport(dialog, "copy-compact-destination-conflict"));
        QCOMPARE(dialog.focusWidget(), path);
        QVERIFY(!QFileInfo::exists(QDir(blocked).filePath("first.txt")));

        QCOMPARE(path->text(), blocked + '/');
        QTest::keyClick(path, Qt::Key_Return);
        QVERIFY(dialog.isVisible());
        QVERIFY(message->isVisible());
        QString destination = blocked + '/';
        if (replacePath) {
            destination = fixture.filePath(QStringLiteral("другая папка")) + '/';
            auto *clipboard = QApplication::clipboard();
            const auto clearClipboard = qScopeGuard([clipboard] { clipboard->clear(); });
            clipboard->setText(destination);
            QTest::keySequence(path, QKeySequence::Paste);
            QCOMPARE(path->text(), destination);
        } else {
            QVERIFY(QDir(blocked).rmdir(secondName));
        }
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), destination);
    }

    void restoredSourceCanBeRetriedWithoutChangingDestination() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        const QString destination = fixture.filePath("new folder") + '/';
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CCopyDialog dialog(false, destination, {source});
        QtCopyDestinationContext context;
        context.LocalFileTransfer = true;
        dialog.ConfigureDestination(context);
        dialog.show();
        QVERIFY(file.remove());
        dialog.accept();
        QVERIFY(dialog.isVisible());
        const auto *error = dialog.findChild<QLabel *>("copyDestinationMessage");
        QVERIFY(error && error->isVisible() && error->text().contains(source));
        QCOMPARE(dialog.findChild<QLineEdit *>("copyDestination")->text(), destination);
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ok->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), destination);
    }

    void blockedDestinationCanBeCorrected_data() {
        QTest::addColumn<bool>("searchDenied");
        QTest::newRow("read-only") << false;
#ifdef Q_OS_UNIX
        QTest::newRow("no-directory-search") << true;
#endif
    }

    void blockedDestinationCanBeCorrected() {
        QFETCH(bool, searchDenied);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString blocked = fixture.filePath("blocked");
        const QString writable =
            searchDenied ? fixture.filePath("writable") : blocked + "/writable";
        QVERIFY(QDir().mkpath(blocked));
        QVERIFY(QDir().mkpath(writable));
        const auto permissions = QFile::permissions(blocked);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(blocked, permissions); });
        const auto blockedPermissions =
            QFile::ReadOwner | (searchDenied ? QFile::WriteOwner : QFile::ExeOwner);
        const auto applyPermissions = [&] {
            if (!QFile::setPermissions(blocked, blockedPermissions))
                return false;
            const QFileInfo directory(blocked);
            return searchDenied ? directory.isWritable() && !directory.isExecutable()
                                : !directory.isWritable();
        };
        if (!applyPermissions())
            QSKIP("The test filesystem does not enforce this directory permission");

        const QString source = fixture.filePath("source.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString target = writable + "/writable target.txt";
        CCopyDialog dialog(false, blocked + "/new folder/target.txt", {source});
        QtCopyDestinationContext context{fixture.path(), "source.txt", blocked + '/', true, {}};
        context.PeerIsFilesystem = true;
        context.LocalFileTransfer = true;
        dialog.ConfigureDestination(context);
        dialog.show();
        auto *path = dialog.findChild<QLineEdit *>("copyDestination");
        auto *peer = dialog.findChild<QPushButton *>("copyPeerDestination");
        QVERIFY(peer->isVisible());
        QVERIFY(!peer->isEnabled());
        auto *ok =
            dialog.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok);
        for (const QString &unwritable :
             {blocked + "/new folder/target.txt", blocked + "/new folder/deeper/"}) {
            path->setText(unwritable);
            QVERIFY(!ok->isEnabled());
            dialog.accept();
            QVERIFY(dialog.isVisible());
            QCOMPARE(path->text(), unwritable);
            QCOMPARE(
                dialog.findChild<QLabel *>("copyDestinationMessage")->text(),
                QString(searchDenied ? "Cannot access folder: %1" : "The folder is read-only: %1")
                    .arg(blocked));
        }
        path->setText(target);
        QVERIFY(ok->isEnabled());
        QWidget other;
        other.show();
        QVERIFY(WindowActivation::waitForActive(dialog));
        path->setText(blocked + "/new folder/target.txt");
        QVERIFY(!ok->isEnabled());
        QVERIFY(WindowActivation::waitForActive(other));
        QVERIFY(QFile::setPermissions(blocked, permissions));
        QVERIFY(WindowActivation::waitForActive(dialog));
        QTRY_VERIFY(ok->isEnabled());
        QVERIFY(peer->isEnabled());
        QCOMPARE(path->text(), blocked + "/new folder/target.txt");
        peer->click();
        QCOMPARE(path->text(), context.PeerDestination);
        QVERIFY(ok->isEnabled());
        QVERIFY(WindowActivation::waitForActive(other));
        QVERIFY(applyPermissions());
        QVERIFY(WindowActivation::waitForActive(dialog));
        QTRY_VERIFY(!ok->isEnabled());
        QVERIFY(!peer->isEnabled());
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
        path->setFocus();
        path->setSelection(5, -4);
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
                QCOMPARE(picker->directory().canonicalPath(), QDir(fixture.path()).canonicalPath());
                if (accept) {
                    picker->setDirectory(selected);
                    picker->selectFile(selected);
                    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
                }
            });
            QTest::mouseClick(dialog.findChild<QPushButton *>("copyBrowseButton"), Qt::LeftButton);
            QVERIFY(found);
            QCOMPARE(name->text(), QString("draft.txt"));
            if (accept)
                QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(path->text())), selected);
            else {
                QCOMPARE(path->text(), destination);
                QCOMPARE(path->selectedText(), destination.mid(1, 4));
                QCOMPARE(path->cursorPosition(), 1);
            }
        }
        QCOMPARE(ToQString(dialog.Value), fixture.path() + '/');
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(ToQString(dialog.Value))),
                 QDir(selected).filePath("draft.txt"));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CopyDestinationTests)
#include "copy_destination_tests.moc"
