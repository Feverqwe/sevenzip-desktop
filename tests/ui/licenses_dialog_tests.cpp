#include "dialogs/licenses/qtlicensesdialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include "ui/support/license_documents_fixture.h"
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

using namespace LicenseDocumentsFixture;
namespace {
QString DisplayedText(const QPlainTextEdit &edit) {
    QString text = edit.document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}
} // namespace

class LicensesDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray previousSettings_;
    std::unique_ptr<QMimeData> savedClipboard_;
  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
        savedClipboard_ = std::make_unique<QMimeData>();
        auto *clipboard = QApplication::clipboard();
        if (clipboard->mimeData())
            for (const auto &format : clipboard->mimeData()->formats())
                savedClipboard_->setData(format, clipboard->mimeData()->data(format));
    }
    void cleanupTestCase() {
        QApplication::clipboard()->setMimeData(savedClipboard_.release());
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void init() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void searchSelectionAndFullCopy() {
        auto *clipboard = QApplication::clipboard();
        QTemporaryDir fixture;
        const QString full = QStringLiteral("<b>Original\u00a0license δ</b>\n").repeated(1000);
        QVERIFY(Write(fixture.filePath("A/GPL.txt"), full.toUtf8()));
        QVERIFY(Write(fixture.filePath("B/MIT.txt"), "other license\n"));
        QVERIFY(Index(fixture.path(), {"A/GPL.txt", "B/MIT.txt", "missing.txt"}));
        QtLicensesDialog dialog(nullptr, fixture.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *search = dialog.findChild<QLineEdit *>("licensesSearch");
        auto *list = dialog.findChild<QListWidget *>("licensesDocuments");
        auto *text = dialog.findChild<QPlainTextEdit *>("licensesText");
        auto *copy = dialog.findChild<QPushButton *>("licensesCopy");
        QVERIFY(search && list && text && copy);
        QVERIFY(text->isReadOnly());
        QCOMPARE(DisplayedText(*text), full);
        auto cursor = text->textCursor();
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        text->copy();
        QCOMPARE(clipboard->text(), full.left(5));
        copy->click();
        QCOMPARE(clipboard->text(), full);
        search->setText("mit");
        QCOMPARE(list->count(), 1);
        QCOMPARE(text->toPlainText(), QString("other license\n"));
        QTest::keyClick(search, Qt::Key_Down);
        QTRY_VERIFY(list->hasFocus());
        search->clear();
        QCOMPARE(text->toPlainText(), QString("other license\n"));
        search->setText("missing");
        QVERIFY(text->toPlainText().isEmpty());
        QVERIFY(!copy->isEnabled());
        QVERIFY(UiInspection::writeReport(dialog, "licenses-unavailable-document"));
        copy->click();
        QCOMPARE(clipboard->text(), full);
        search->setText("no matches");
        QCOMPARE(list->count(), 0);
        QVERIFY(text->toPlainText().isEmpty());
        QVERIFY(!copy->isEnabled());
        QVERIFY(UiInspection::writeReport(dialog, "licenses-empty-search"));
        search->setText("gpl");
        QCOMPARE(DisplayedText(*text), full);
        QVERIFY(copy->isEnabled());
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(!dialog.isVisible());
    }
    void independentWindowsAndEmptyStates() {
        QTemporaryDir fixture, missing;
        QVERIFY(Write(fixture.filePath("A.txt"), "first"));
        QVERIFY(Write(fixture.filePath("B.txt"), "second"));
        QVERIFY(Index(fixture.path(), {"A.txt", "B.txt"}));
        QtLicensesDialog first(nullptr, fixture.path()), second(nullptr, fixture.path());
        first.findChild<QLineEdit *>("licensesSearch")->setText("B.txt");
        QCOMPARE(first.findChild<QPlainTextEdit *>("licensesText")->toPlainText(),
                 QString("second"));
        QCOMPARE(second.findChild<QPlainTextEdit *>("licensesText")->toPlainText(),
                 QString("first"));
        QtLicensesDialog unavailable(nullptr, missing.path());
        QVERIFY(!unavailable.findChild<QLabel *>("licensesMessage")->text().isEmpty());
        QVERIFY(!unavailable.findChild<QPushButton *>("licensesCopy")->isEnabled());
        QVERIFY(unavailable.findChild<QPlainTextEdit *>("licensesText")->toPlainText().isEmpty());
        QVERIFY(Index(missing.path(), {}));
        QtLicensesDialog empty(nullptr, missing.path());
        QVERIFY(!empty.findChild<QLabel *>("licensesMessage")->text().isEmpty());
        QVERIFY(!empty.findChild<QPushButton *>("licensesCopy")->isEnabled());
        QVERIFY(empty.findChild<QPlainTextEdit *>("licensesText")->toPlainText().isEmpty());
        QVERIFY(empty.findChild<QLabel *>("licensesMessage")->text() !=
                unavailable.findChild<QLabel *>("licensesMessage")->text());
    }
    void installedPayload() {
        const QString root = qEnvironmentVariable("SEVENZIP_INSTALLED_NOTICES");
        if (root.isEmpty())
            QSKIP("Select an installed notices payload with SEVENZIP_INSTALLED_NOTICES.");
        QtLicenseDocuments documents(root);
        QString error;
        QVERIFY2(documents.load(error), qPrintable(error));
        QVERIFY(!documents.documents().isEmpty());
        QtLicensesDialog dialog(nullptr, root);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *list = dialog.findChild<QListWidget *>("licensesDocuments");
        auto *text = dialog.findChild<QPlainTextEdit *>("licensesText");
        auto *copy = dialog.findChild<QPushButton *>("licensesCopy");
        QCOMPARE(list->count(), documents.documents().size());
        for (int row = 0; row < list->count(); ++row) {
            list->setCurrentRow(row);
            QFile file(QDir(root).filePath(documents.documents()[row].path));
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
            const QString original = QString::fromUtf8(file.readAll());
            QString expected = original;
            expected.replace("\r\n", "\n");
            expected.replace('\r', '\n');
            QCOMPARE(DisplayedText(*text), expected);
            QVERIFY(copy->isEnabled());
            copy->click();
            QCOMPARE(QApplication::clipboard()->text(), original);
        }
        list->setCurrentRow(0);
        for (const QSize size : {QSize(860, 600), QSize(480, 640)}) {
            dialog.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::writeReport(dialog, "licenses-installed-" +
                                                          QString::number(size.width())));
        }
    }
    void layoutsAndLanguage() {
        QTemporaryDir fixture;
        const QString path = QStringLiteral("Компонент с длинным именем 空/GPL-2.0.txt");
        const QString original = QStringLiteral("Original <license> δ\n").repeated(100);
        QVERIFY(Write(fixture.filePath(path), original.toUtf8()));
        QVERIFY(Index(fixture.path(), {path, "missing.txt"}));
        QtLicensesDialog dialog(nullptr, fixture.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            for (const QSize size : {QSize(860, 600), QSize(480, 640)}) {
                dialog.resize(size);
                QApplication::processEvents();
                auto *splitter = dialog.findChild<QSplitter *>("licensesSplitter");
                QCOMPARE(splitter->orientation(),
                         size.width() < 600 ? Qt::Vertical : Qt::Horizontal);
                for (const char *name :
                     {"licensesSearch", "licensesDocuments", "licensesText", "licensesCopy"}) {
                    auto *control = dialog.findChild<QWidget *>(name);
                    QVERIFY(control);
                    QTRY_VERIFY2(UiInspection::fullyVisible(*control, dialog), name);
                }
                QCOMPARE(dialog.findChild<QPlainTextEdit *>("licensesText")->toPlainText(),
                         original);
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("licenses-%1-%2").arg(language).arg(size.width())));
            }
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LicensesDialogTests)
#include "licenses_dialog_tests.moc"
