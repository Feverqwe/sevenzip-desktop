#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "dialogs/link/LinkDialog.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/dialogs/qtdialogunits.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QStyleOptionGroupBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class LinkDialogTests final : public QObject {
    Q_OBJECT
    static bool readTarget(const QString &path, UString &target) {
        const FString native =
            us2fs(UString(QDir::toNativeSeparators(path).toStdWString().c_str()));
#ifdef _WIN32
        CByteBuffer data;
        if (!NWindows::NFile::NIO::GetReparseData(native, data, nullptr))
            return false;
        NWindows::NFile::CReparseAttr attr;
        if (!attr.Parse(data, data.Size()))
            return false;
        target = attr.GetPath();
        return true;
#else
        return QtGetPosixLinkTarget(native, target);
#endif
    }
    static int run(CLinkDialog &dialog, const QString &from, const QString &to, unsigned type,
                   bool cancel = false, QString *error = nullptr) {
        bool entered = false;
        QTimer responder;
        QObject::connect(&responder, &QTimer::timeout, &dialog, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (error)
                    *error = box->text();
                box->accept();
                QTimer::singleShot(0, &dialog, &QDialog::reject);
            } else if (QApplication::activeModalWidget() == &dialog && !entered) {
                entered = true;
                dialog.findChild<QComboBox *>("IDC_LINK_PATH_FROM")->setEditText(from);
                dialog.findChild<QComboBox *>("IDC_LINK_PATH_TO")->setEditText(to);
                for (auto *radio : dialog.findChildren<QRadioButton *>())
                    if (radio->property("languageID").toUInt() == type)
                        radio->setChecked(true);
                auto *button =
                    dialog.findChild<QPushButton *>(cancel ? "IDCANCEL" : "IDB_LINK_LINK");
                QTimer::singleShot(0, button, &QPushButton::click);
            }
        });
        responder.start(1);
        return int(dialog.Create());
    }

  private slots:
    void geometryAndLanguage_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const char *language : {"-", "ru"})
            for (const QSize size : {QSize(520, 500), QSize(360, 300)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(size.width())))
                    << QString::fromLatin1(language) << size;
    }
    void geometryAndLanguage() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        CLinkDialog dialog;
        bool inspected = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &dialog, [&] {
            if (QApplication::activeModalWidget() != &dialog || inspected)
                return;
            inspected = true;
            struct Close {
                QDialog &Dialog;
                ~Close() { Dialog.reject(); }
            } close{dialog};
            auto *from = dialog.findChild<QComboBox *>("IDC_LINK_PATH_FROM");
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            QVERIFY(from->hasFocus() || from->lineEdit()->hasFocus());
            from->setEditText(QStringLiteral("pending link 空"));
            SaveRegLang(UString(language.toStdWString().c_str()),
                        qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            QCOMPARE(from->currentText(), QStringLiteral("pending link 空"));
            QCOMPARE(dialog.windowTitle(),
                     language == "ru" ? QStringLiteral("Ссылка") : QString("Link"));
            QCOMPARE(dialog.findChild<QPushButton *>("IDCANCEL")->text(),
                     language == "ru" ? QStringLiteral("Отмена") : QString("Cancel"));
#ifndef _WIN32
            for (const char *name : {"IDR_LINK_TYPE_JUNCTION", "IDR_LINK_TYPE_WSL"}) {
                auto *radio = dialog.findChild<QRadioButton *>(name);
                QVERIFY(!radio->isEnabled());
                QVERIFY(!radio->toolTip().isEmpty());
            }
#endif
            QApplication::processEvents();
            QVERIFY(UiInspection::writeReport(
                dialog, QString("link-source-frame-%1-%2").arg(language).arg(size.width())));
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinMaxButtonsHint));
            QVERIFY(UiInspection::fullyVisible(*from, dialog));
            QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QGroupBox *>("IDG_LINK_TYPE"),
                                               dialog));
            dialog.resize(size.width(), qMin(size.height(), dialog.sizeHint().height()));
            QApplication::processEvents();
            // Focused editable combos can asynchronously scroll themselves into view.
            dialog.setFocus();
            QApplication::processEvents();
            auto *scroll = dialog.findChild<QScrollArea *>("linkControlsScroll");
            QVERIFY(UiInspection::writeReport(
                dialog, QString("link-initial-%1-%2").arg(language).arg(size.width())));
            for (QWidget *control : scroll->widget()->findChildren<QWidget *>()) {
                if (control->objectName().isEmpty() || !control->isVisible())
                    continue;
                if (auto *group = qobject_cast<QGroupBox *>(control)) {
                    // The frame can exceed a compact viewport; check its title
                    // here and each child independently in subsequent iterations.
                    QStyleOptionGroupBox option;
                    option.initFrom(group);
                    option.text = group->title();
                    const QRect title = group->style()->subControlRect(
                        QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, group);
                    const QPoint center = group->mapTo(scroll->widget(), title.center());
                    scroll->ensureVisible(center.x(), center.y(), title.width() / 2 + 5,
                                          title.height() / 2 + 5);
                    QApplication::processEvents();
                    QVERIFY(scroll->viewport()->rect().contains(
                        QRect(group->mapTo(scroll->viewport(), title.topLeft()), title.size())));
                    continue;
                }
                scroll->ensureWidgetVisible(control, 5, 5);
                QApplication::processEvents();
                QVERIFY2(UiInspection::fullyVisible(*control, dialog),
                         qPrintable(control->objectName()));
                if (qobject_cast<QLabel *>(control))
                    QVERIFY2(UiInspection::labelFits(*control), qPrintable(control->objectName()));
                if (auto *button = qobject_cast<QAbstractButton *>(control)) {
                    if (button->objectName() == "IDB_LINK_PATH_FROM" ||
                        button->objectName() == "IDB_LINK_PATH_TO") {
                        QCOMPARE(button->width(),
                                 QtDialogUnits(dialog.font()).pixels(QSize(20, 0)).width());
                        QStyleOptionButton option;
                        option.initFrom(button);
                        option.text = button->text();
                        const QRect contents = button->style()->subElementRect(
                            QStyle::SE_PushButtonContents, &option, button);
                        QVERIFY(button->fontMetrics().horizontalAdvance(button->text()) <=
                                contents.width());
                    } else
                        QVERIFY2(button->width() >= button->sizeHint().width(),
                                 qPrintable(button->objectName()));
                }
            }
            for (const char *name : {"IDB_LINK_LINK", "IDCANCEL"})
                QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QPushButton *>(name), dialog));
            scroll->verticalScrollBar()->setValue(0);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*from, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog, QString("link-%1-%2").arg(language).arg(size.width())));
        });
        responder.start(1);
        QCOMPARE(dialog.Create(), qintptr(QDialog::Rejected));
        QVERIFY(inspected);
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void sourceCreateAndCancel_data() {
        QTest::addColumn<unsigned>("type");
        QTest::addColumn<bool>("relative");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("hard") << unsigned(IDR_LINK_TYPE_HARD) << false << false;
        QTest::newRow("file-symbolic") << unsigned(IDR_LINK_TYPE_SYM_FILE) << false << false;
        QTest::newRow("relative-symbolic") << unsigned(IDR_LINK_TYPE_SYM_FILE) << true << false;
        QTest::newRow("cancel") << unsigned(IDR_LINK_TYPE_HARD) << false << true;
    }
    void sourceCreateAndCancel() {
        QFETCH(unsigned, type);
        QFETCH(bool, relative);
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath(QStringLiteral("цель с пробелом.txt"));
        const QString link = directory.filePath(QStringLiteral("ссылка 空.txt"));
        QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        CLinkDialog dialog;
        dialog.CurDirPrefix = (directory.path() + '/').toStdWString().c_str();
        dialog.FilePath = target.toStdWString().c_str();
        QString error;
        QCOMPARE(run(dialog, link, relative ? QFileInfo(target).fileName() : target, type, cancel,
                     &error),
                 int(cancel ? QDialog::Rejected : QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (cancel) {
            QVERIFY(!QFileInfo::exists(link));
            return;
        }
        QFile output(link);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("123456789"));
        output.close();
        if (type == IDR_LINK_TYPE_HARD) {
            QVERIFY(output.open(QIODevice::Append));
            QCOMPARE(output.write("!"), qint64(1));
            output.close();
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("123456789!"));
        } else {
            QVERIFY(QFileInfo(link).isSymLink());
            UString raw;
            QVERIFY(readTarget(link, raw));
            QCOMPARE(QString::fromWCharArray(raw.Ptr()),
                     relative ? QFileInfo(target).fileName() : QDir::toNativeSeparators(target));
        }
    }
    void embeddedTerminatorDoesNotCreateTruncatedLink_data() {
        QTest::addColumn<bool>("from");
        QTest::newRow("link-path") << true;
        QTest::newRow("target-path") << false;
    }
    void embeddedTerminatorDoesNotCreateTruncatedLink() {
        QFETCH(bool, from);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath(QStringLiteral("цель 空.txt"));
        const QString link = directory.filePath(QStringLiteral("ссылка 空.txt"));
        QFile original(target);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("protected bytes"), qint64(15));
        original.close();
        const QString suffix = QChar::Null + QStringLiteral("suffix");
        CLinkDialog rejected;
        rejected.FilePath = target.toStdWString().c_str();
        QString error;
        QCOMPARE(run(rejected, from ? link + suffix : link, from ? target : target + suffix,
                     IDR_LINK_TYPE_HARD, false, &error),
                 int(QDialog::Rejected));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(link));
        QVERIFY(!QFileInfo(link).isSymLink());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("protected bytes"));
        original.close();
        CLinkDialog corrected;
        corrected.FilePath = target.toStdWString().c_str();
        error.clear();
        QCOMPARE(run(corrected, link, target, IDR_LINK_TYPE_HARD, false, &error),
                 int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QFile created(link);
        QVERIFY(created.open(QIODevice::ReadOnly));
        QCOMPARE(created.readAll(), QByteArray("protected bytes"));
    }

    void relativeTargetClassification_data() {
        QTest::addColumn<bool>("directoryTarget");
        QTest::addColumn<bool>("wrongType");
        for (bool directory : {false, true})
            for (bool wrong : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2")
                                             .arg(directory ? "directory" : "file")
                                             .arg(wrong ? "wrong-type" : "create")))
                    << directory << wrong;
    }
    void relativeTargetClassification() {
#ifdef _WIN32
        QSKIP("Relative symbolic target classification is a POSIX adaptation.");
#else
        QFETCH(bool, directoryTarget);
        QFETCH(bool, wrongType);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QDir root(fixture.path());
        QVERIFY(root.mkdir("cwd"));
        QVERIFY(root.mkdir("links"));
        const QString targetName = QStringLiteral("цель 空");
        const QString target = root.filePath("links/" + targetName);
        const QString decoy = root.filePath("cwd/" + targetName);
        const QString filePath = directoryTarget ? decoy : target;
        QVERIFY(QDir().mkdir(directoryTarget ? target : decoy));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("payload"), qint64(7));
        file.close();
        const QString previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(root.filePath("cwd")));
        const QString link = root.filePath(QStringLiteral("links/ссылка"));
        CLinkDialog dialog;
        dialog.FilePath = target.toStdWString().c_str();
        QString error;
        QCOMPARE(run(dialog, link, targetName,
                     directoryTarget != wrongType ? IDR_LINK_TYPE_SYM_DIR : IDR_LINK_TYPE_SYM_FILE,
                     false, &error),
                 int(wrongType ? QDialog::Rejected : QDialog::Accepted));
        if (wrongType) {
            QVERIFY(!error.isEmpty());
            QVERIFY(!QFileInfo(link).isSymLink());
            return;
        }
        QVERIFY2(error.isEmpty(), qPrintable(error));
        UString raw;
        QVERIFY(readTarget(link, raw));
        QCOMPARE(QString::fromWCharArray(raw.Ptr()), targetName);
        QCOMPARE(QFileInfo(link).isDir(), directoryTarget);
        if (!directoryTarget) {
            QFile output(link);
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll(), QByteArray("payload"));
        }
#endif
    }
    void symbolicObjectCases_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"directory", "dangling", "cycle"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
#ifdef _WIN32
        QTest::newRow("junction") << QString("junction");
#else
        QTest::newRow("directory-via-link") << QString("directory-via-link");
#endif
    }
    void symbolicObjectCases() {
        QFETCH(QString, kind);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString link = directory.filePath(QStringLiteral("ссылка 空"));
        const QString target =
            kind == "cycle" ? QFileInfo(link).fileName() : directory.filePath("target");
        const bool directoryTarget =
            kind == "directory" || kind == "junction" || kind == "directory-via-link";
#ifndef _WIN32
        if (kind == "directory-via-link") {
            const QString real = directory.filePath("real directory");
            QVERIFY(QDir().mkdir(real));
            QVERIFY(QtSetPosixLink(UString(target.toStdWString().c_str()),
                                   UString(real.toStdWString().c_str())));
        } else
#endif
            if (directoryTarget)
            QVERIFY(QDir().mkdir(target));
        CLinkDialog dialog;
        dialog.FilePath = target.toStdWString().c_str();
        QString error;
        QCOMPARE(run(dialog, link, target,
                     kind == "junction" ? IDR_LINK_TYPE_JUNCTION
                     : directoryTarget  ? IDR_LINK_TYPE_SYM_DIR
                                        : IDR_LINK_TYPE_SYM_FILE,
                     false, &error),
                 int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        UString raw;
        QVERIFY(readTarget(link, raw));
        QCOMPARE(QString::fromWCharArray(raw.Ptr()), QDir::toNativeSeparators(target));
        if (!directoryTarget) {
            QFile input(link);
            QVERIFY(!input.open(QIODevice::ReadOnly));
        }
    }
    void retargetAndClear() {
        QTemporaryDir directory;
        const QString path = directory.filePath("link");
        const UString from(path.toStdWString().c_str());
#ifdef _WIN32
        CLinkDialog create;
        QString error;
        QCOMPARE(run(create, path, QString("old missing"), IDR_LINK_TYPE_SYM_FILE, false, &error),
                 int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        CLinkDialog edit;
        edit.FilePath = from;
        QCOMPARE(run(edit, path, QString("new missing"), IDR_LINK_TYPE_SYM_FILE, false, &error),
                 int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
#else
        QVERIFY(QtSetPosixLink(from, UString("old missing")));
        QVERIFY(QtSetPosixLink(from, UString("new missing")));
#endif
        UString raw;
        QVERIFY(readTarget(path, raw));
        QCOMPARE(QString::fromWCharArray(raw.Ptr()), QString("new missing"));
        QCOMPARE(
            QDir(directory.path())
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot),
            QStringList{"link"});
#ifdef _WIN32
        CLinkDialog clear;
        clear.FilePath = from;
        QCOMPARE(run(clear, path, QString(), IDR_LINK_TYPE_SYM_FILE, false, &error),
                 int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // DeleteReparseData preserves the original empty file object on Windows.
        QVERIFY(QFileInfo(path).isFile());
        QCOMPARE(QFileInfo(path).size(), qint64(0));
#else
        QVERIFY(QtSetPosixLink(from, UString()));
#endif
        QVERIFY(!QFileInfo(path).isSymLink());
    }
    void refusesHidingExistingBytes_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");
        QTest::newRow("en") << QString("-")
                            << QString(
                                   "WARNING: reparse point will hide the data of existing file");
        QTest::newRow("ru")
            << QString("ru")
            << QString::fromUtf8(
                   "ПРЕДУПРЕЖДЕНИЕ: точка повторной обработки скроет данные существующего файла");
    }
    void refusesHidingExistingBytes() {
        QFETCH(QString, language);
        QFETCH(QString, expected);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir directory;
        const QString path = directory.filePath("existing.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("preserve"), qint64(8));
        file.close();
        CLinkDialog dialog;
        dialog.FilePath = path.toStdWString().c_str();
        QString error;
        QCOMPARE(
            run(dialog, path, directory.filePath("missing"), IDR_LINK_TYPE_SYM_FILE, false, &error),
            int(QDialog::Rejected));
        QCOMPARE(error, expected + '\n' + QDir::toNativeSeparators(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("preserve"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LinkDialogTests)
#include "link_dialog_tests.moc"
