#include "dialogs/copy/CopyDialog.h"
#include "dialogs/split/SplitDialog.h"
#include "fileops/volumes/nativevolumeworker.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "support/split_dialog_prompt.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class VolumeDialogTests : public QObject {
    Q_OBJECT
  private slots:
    void initialFieldsStayReadableAndResize() {
        const QFont originalFont = QApplication::font();
        const auto restoreFont = qScopeGuard([&] { QApplication::setFont(originalFont); });
        int smallHeight = 0;
        for (int points : {8, 12}) {
            QFont font = originalFont;
            font.setPointSize(points);
            QApplication::setFont(font);
            CSplitDialog dialog(QStringLiteral("файл 空.bin"), "/temporary/destination");
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
            QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
            dialog.show();
            QApplication::processEvents();
            auto *path = dialog.findChild<QComboBox *>("volumeDestination");
            auto *volume = dialog.findChild<QComboBox *>("volumeSize");
            auto *browse = dialog.findChild<QPushButton *>("volumeBrowse");
            const int volumeWidth = volume->width();
            const int browseWidth = browse->width();
            for (QWidget *field : QList<QWidget *>{path, volume, browse})
                QVERIFY(UiInspection::fullyVisible(*field, dialog));
            const int pathWidth = path->width();
            const int initialHeight = dialog.height();
            dialog.resize(dialog.width() + 180, dialog.height());
            // XCB's window manager publishes resize asynchronously.
            QTRY_VERIFY(path->width() > pathWidth);
            QTRY_VERIFY(volume->width() > volumeWidth);
            QCOMPARE(browse->width(), browseWidth);
            QVERIFY(
                UiInspection::writeReport(dialog, QString("split-source-fields-%1").arg(points)));
            if (points == 8)
                smallHeight = initialHeight;
            else
                QVERIFY(initialHeight > smallHeight);
            dialog.reject();
        }
    }

    void splitKeepsUnsignedSourceSizes() {
        CSplitDialog dialog("source.bin", "/not-created");
        dialog.findChild<QComboBox *>("volumeSize")->setCurrentText("2 9223372036854775808");
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.VolumeSizes.Size(), 2u);
        QCOMPARE(dialog.VolumeSizes[0], UInt64(2));
        QCOMPARE(dialog.VolumeSizes[1], UInt64(1) << 63);
    }

    void splitUsesSourceTrimAndAllowsRetry_data() {
        QTest::addColumn<QString>("bad");
        QTest::newRow("leading-nbsp") << QString(QChar(0x00a0)) + "10";
        QTest::newRow("leading-cr") << QString("\r10");
        QTest::newRow("embedded-nul") << QString("10") + QChar::Null + " 20";
        QTest::newRow("nul-after-unit") << QString("10M") + QChar::Null + " 20M";
    }
    void splitUsesSourceTrimAndAllowsRetry() {
        QFETCH(QString, bad);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CSplitDialog dialog("source.bin", temp.filePath("not created"));
        dialog.show();
        auto *volume = dialog.findChild<QComboBox *>("volumeSize");
        volume->setCurrentText(bad);
        const auto prompt = SubmitInvalidSplit(dialog);
        QVERIFY(prompt.shown);
        QVERIFY(prompt.sourcePolicy);
        QVERIFY(dialog.isVisible());
        QVERIFY(!QFileInfo::exists(dialog.Path));
        // UString::Trim removes precisely space, LF and tab.
        volume->setCurrentText(" \n\t10 20\t\n ");
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.VolumeSizes.Size(), 2u);
        QCOMPARE(dialog.VolumeSizes[0], UInt64(10));
        QCOMPARE(dialog.VolumeSizes[1], UInt64(20));
        QVERIFY(!QFileInfo::exists(dialog.Path));
    }

    void destinationRejectsNulAndAllowsRetry_data() {
        QTest::addColumn<bool>("combine");
        QTest::newRow("split") << false;
        QTest::newRow("combine") << true;
    }
    void destinationRejectsNulAndAllowsRetry() {
        QFETCH(bool, combine);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        std::unique_ptr<QDialog> dialog;
        if (combine)
            dialog = std::make_unique<CCopyDialog>("Combine", "Combine to:", fixture.path(), "");
        else
            dialog = std::make_unique<CSplitDialog>("source.bin", fixture.path());
        auto *path =
            dialog->findChild<QComboBox *>(combine ? "copyDestination" : "volumeDestination");
        const QString invalid = fixture.filePath("shortened") + QChar::Null + "suffix";
        path->setCurrentText(invalid);
        dialog->show();
        bool reported = false;
        QTimer::singleShot(0, dialog.get(), [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                reported = message->icon() == QMessageBox::Critical;
                message->accept();
            }
        });
        dialog->accept();
        QVERIFY(reported);
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(dialog->isVisible());
        QCOMPARE(path->currentText(), invalid);
        const QString corrected = fixture.filePath(QStringLiteral("результат 空"));
        path->setCurrentText(corrected);
        dialog->accept();
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        const QString accepted = combine ? ToQString(static_cast<CCopyDialog &>(*dialog).Value)
                                         : static_cast<CSplitDialog &>(*dialog).Path;
        QCOMPARE(accepted, corrected);
    }

    void splitInitAndConfirmation_data() {
        QTest::addColumn<bool>("viaButton");
        QTest::newRow("accept") << false;
        QTest::newRow("button") << true;
    }
    void splitInitAndConfirmation() {
        QFETCH(bool, viaButton);
        QTemporaryDir temp;
        CSplitDialog dialog(QStringLiteral("файл с пробелом.bin"), temp.path());
        QVERIFY(dialog.windowTitle().endsWith("файл с пробелом.bin"));
        auto *path = dialog.findChild<QComboBox *>("volumeDestination");
        auto *volume = dialog.findChild<QComboBox *>("volumeSize");
        QVERIFY(path && volume);
        QCOMPARE(path->currentText(), temp.path());
        path->setCurrentText(temp.filePath(QStringLiteral("новая папка")));
        volume->setCurrentText(" 10M 20M ");
        if (viaButton)
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        else
            dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.Path, path->currentText());
        QCOMPARE(dialog.VolumeSizes.Size(), 2u);
        QCOMPARE(dialog.VolumeSizes[0], UInt64(10) << 20);
        QCOMPARE(dialog.VolumeSizes[1], UInt64(20) << 20);
        QVERIFY(!QFileInfo::exists(dialog.Path));
    }

    void splitRejectsInvalidSizesAndCancelDoesNotCreateDirectory() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("не создана"));
        CSplitDialog dialog("source.bin", path);
        dialog.show();
        auto *volume = dialog.findChild<QComboBox *>("volumeSize");
        for (const QString &bad : {QString(), QString("0"), QString("10M 0M"),
                                   QString("18446744073709551616"), QString("wrong")}) {
            volume->setCurrentText(bad);
            const auto prompt = SubmitInvalidSplit(dialog, {}, bad.isEmpty());
            QVERIFY(prompt.shown);
            QVERIFY(prompt.sourcePolicy);
            QVERIFY(dialog.isVisible());
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QCOMPARE(volume->currentText(), bad);
            QVERIFY(!QFileInfo::exists(path));
        }
        dialog.reject();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!QFileInfo::exists(path));
    }

    void splitGeometry_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("invalid");
        for (int width : {480, 340})
            for (bool invalid : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(width).arg(invalid)))
                    << width << invalid;
    }
    void splitGeometry() {
        QFETCH(int, width);
        QFETCH(bool, invalid);
        CSplitDialog dialog("source.bin", "/temporary/destination");
        auto *path = dialog.findChild<QComboBox *>("volumeDestination");
        auto *volume = dialog.findChild<QComboBox *>("volumeSize");
        auto *browse = dialog.findChild<QPushButton *>("volumeBrowse");
        auto *pathLabel = dialog.findChild<QLabel *>("splitPathLabel");
        auto *volumeLabel = dialog.findChild<QLabel *>("splitVolumeLabel");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("splitButtons");
        dialog.resize(width, 240);
        dialog.show();
        QApplication::processEvents();
        if (invalid) {
            volume->setCurrentText("wrong");
            const auto prompt =
                SubmitInvalidSplit(dialog, QStringLiteral("split-invalid-volume-%1").arg(width));
            QVERIFY(prompt.shown);
            QVERIFY(prompt.sourcePolicy);
            QVERIFY(prompt.geometry);
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QVERIFY(dialog.isVisible());
        }
        QVERIFY(UiInspection::fullyVisible(*path, dialog));
        QVERIFY(UiInspection::fullyVisible(*volume, dialog));
        QVERIFY(UiInspection::fullyVisible(*browse, dialog));
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(UiInspection::labelFits(*pathLabel));
        QVERIFY(UiInspection::labelFits(*volumeLabel));
        QVERIFY(UiInspection::bounds(*pathLabel, dialog).bottom() <
                UiInspection::bounds(*path, dialog).top());
        QVERIFY(UiInspection::bounds(*path, dialog).bottom() <
                UiInspection::bounds(*volumeLabel, dialog).top());
        QVERIFY(UiInspection::bounds(*volumeLabel, dialog).bottom() <
                UiInspection::bounds(*volume, dialog).top());
        QVERIFY(UiInspection::separate(*path, *browse, dialog));
        QVERIFY(UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                       *buttons->button(QDialogButtonBox::Cancel), dialog));
        QVERIFY(UiInspection::writeReport(
            dialog, QStringLiteral("split-%1-%2").arg(width).arg(invalid ? "error" : "normal")));
    }

    void combineUsesCopyDialogAndUpstreamSummary_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("two") << 2;
        QTest::newRow("three") << 3;
        QTest::newRow("many") << 5;
    }
    void combineUsesCopyDialogAndUpstreamSummary() {
        QFETCH(int, count);
        CCombineVolumePlan combiner;
        combiner.InputDirPrefix = us2fs(UString("/parts/"));
        combiner.TotalSize = 1234;
        for (int i = 1; i <= count; ++i)
            combiner.Names.Add(us2fs(
                ToUString(QStringLiteral("часть с пробелом.%1").arg(i, 3, 10, QLatin1Char('0')))));
        const QString info = ToQString(combiner.GetInfo());
        QVERIFY(info.contains(QStringLiteral("Files: %1").arg(count)));
        QVERIFY(info.contains(QLocale().toString(1234) + " bytes"));
        QVERIFY(info.contains("\n/parts/"));
        QVERIFY(info.contains(ToQString(fs2us(combiner.Names.Front()))));
        QVERIFY(info.contains(ToQString(fs2us(combiner.Names[1]))));
        QVERIFY(info.contains(ToQString(fs2us(combiner.Names.Back()))));
        QCOMPARE(info.contains("..."), count > 3);
        if (count == 5)
            QVERIFY(!info.contains(ToQString(fs2us(combiner.Names[2]))));
        CCopyDialog dialog("Объединить файлы часть.001", "Объединить в:", "/output", info);
        auto *summary = dialog.findChild<QPlainTextEdit *>("copySelectionInfo");
        QCOMPARE(summary->toPlainText(), QString(info).replace(QChar::Nbsp, QLatin1Char(' ')));
        QVERIFY(summary->isReadOnly());
        for (int width : {480, 340}) {
            dialog.resize(width, 250);
            dialog.show();
            QApplication::processEvents();
            auto *buttons = dialog.findChild<QDialogButtonBox *>("copyButtons");
            QVERIFY(UiInspection::fullyVisible(*summary, dialog));
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
            QVERIFY(UiInspection::separate(*summary, *buttons, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog, QStringLiteral("combine-%1-%2").arg(count).arg(width)));
        }
        dialog.findChild<QComboBox *>("copyDestination")->setCurrentText("/new destination");
        dialog.accept();
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("/new destination"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(VolumeDialogTests)
#include "volume_dialog_tests.moc"
