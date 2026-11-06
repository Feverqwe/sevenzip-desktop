#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/extract/alias.h"
#include "support/archive_expert.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
#include <memory>

class UiGeometryTests final : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QByteArray previousSettings_;

    static bool footerFits(QDialog &dialog) {
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        return buttons && UiInspection::fullyVisible(*buttons, dialog) &&
               UiInspection::fullyVisible(*buttons->button(QDialogButtonBox::Ok), dialog) &&
               UiInspection::fullyVisible(*buttons->button(QDialogButtonBox::Cancel), dialog) &&
               UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                      *buttons->button(QDialogButtonBox::Cancel), dialog);
    }

  private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", directory_.filePath("settings.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }

    void detectsClippingOverlapAndHiddenControls() {
        QWidget root;
        root.resize(200, 100);
        QWidget container(&root);
        container.setGeometry(10, 10, 100, 60);
        QLabel first("Short", &container);
        first.setGeometry(0, 0, 70, 25);
        QLabel second("Other", &container);
        second.setGeometry(50, 0, 70, 25);
        root.show();
        QApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(first, root));
        QVERIFY(!UiInspection::fullyVisible(second, root));
        QVERIFY(!UiInspection::separate(first, second, root));
        second.move(0, 30);
        QVERIFY(UiInspection::fullyVisible(second, root));
        QVERIFY(UiInspection::separate(first, second, root));
        first.setText("A label that cannot fit in the allocated width");
        QVERIFY(!UiInspection::labelFits(first));
        second.hide();
        QVERIFY(!UiInspection::fullyVisible(second, root));
    }

    void reportOmitsEditableValues() {
        const QByteArray previousReports = qgetenv("SEVENZIP_UI_REPORTS");
        qputenv("SEVENZIP_UI_REPORTS", "ON");
        const auto restoreReports = qScopeGuard([&] {
            if (previousReports.isNull())
                qunsetenv("SEVENZIP_UI_REPORTS");
            else
                qputenv("SEVENZIP_UI_REPORTS", previousReports);
        });
        QWidget root;
        QLineEdit password(&root);
        password.setObjectName("password");
        password.setText("secret-value");
        password.setEchoMode(QLineEdit::Normal);
        QComboBox path(&root);
        path.setEditable(true);
        path.setEditText("private-path");
        const QString report = UiInspection::dump(root);
        QVERIFY(report.contains("password"));
        QVERIFY(!report.contains("secret-value"));
        QVERIFY(!report.contains("private-path"));
        QVERIFY(UiInspection::writeReport(root, "geometry-private-values"));
        QFile saved(
            qEnvironmentVariable("SEVENZIP_UI_REPORT_DIR", QDir::currentPath() + "/ui-reports") +
            "/geometry-private-values.json");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QJsonDocument document = QJsonDocument::fromJson(saved.readAll());
        QVERIFY(document.isObject());
        QCOMPARE(document, QJsonDocument::fromJson(report.toUtf8()));
    }

    void disabledReportsDoNotCreateOutput() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto previousReports = qgetenv("SEVENZIP_UI_REPORTS");
        const auto previousScreenshots = qgetenv("SEVENZIP_CAPTURE_SCREENSHOTS");
        const auto previousDirectory = qgetenv("SEVENZIP_UI_REPORT_DIR");
        const auto restore = qScopeGuard([&] {
            for (const auto &entry :
                 {qMakePair("SEVENZIP_UI_REPORTS", previousReports),
                  qMakePair("SEVENZIP_CAPTURE_SCREENSHOTS", previousScreenshots),
                  qMakePair("SEVENZIP_UI_REPORT_DIR", previousDirectory)}) {
                if (entry.second.isNull())
                    qunsetenv(entry.first);
                else
                    qputenv(entry.first, entry.second);
            }
        });
        qunsetenv("SEVENZIP_UI_REPORTS");
        qunsetenv("SEVENZIP_CAPTURE_SCREENSHOTS");
        const auto destination = temporary.filePath("reports");
        qputenv("SEVENZIP_UI_REPORT_DIR", destination.toUtf8());
        QWidget root;
        QVERIFY(UiInspection::writeReport(root, "disabled"));
        QVERIFY(!QDir(destination).exists());
    }

    void dialogs_data() {
        QTest::addColumn<bool>("create");
        QTest::addColumn<QSize>("requestedSize");
        QTest::newRow("create-normal") << true << QSize(760, 720);
        QTest::newRow("create-compact") << true << QSize(640, 480);
        QTest::newRow("extract-normal") << false << QSize(640, 360);
        QTest::newRow("extract-compact") << false << QSize(470, 330);
    }

    void addDefaultSizeFitsScreen() {
        CreateArchiveDialog dialog(directory_.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        // Nested native form layouts can settle after the first activation.
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "create-default"));
        QVERIFY(dialog.screen()->availableGeometry().contains(dialog.frameGeometry()));
        QVERIFY(footerFits(dialog));
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        // The default form should not reserve a large empty picker area.
        QVERIFY(dialog.height() < 720);
    }

    void dialogs() {
        QFETCH(bool, create);
        QFETCH(QSize, requestedSize);
        std::unique_ptr<QDialog> dialog;
        if (create)
            dialog = std::make_unique<CreateArchiveDialog>(directory_.path());
        else
            dialog = std::make_unique<ExtractDialog>(directory_.path(), QStringList{});
        auto *advanced = dialog->findChild<QToolButton *>("extractAdvancedToggle");
        auto *passwordEnabled = dialog->findChild<QCheckBox *>(create ? "archivePasswordEnabled"
                                                                      : "extractPasswordEnabled");
        QVERIFY(passwordEnabled);
        if (create)
            ExpandArchiveSections(*dialog);
        else {
            QVERIFY(advanced);
            advanced->click();
        }
        passwordEnabled->click();
        dialog->resize(requestedSize);
        dialog->show();
        QApplication::processEvents();
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        // A native window manager can acknowledge minimum-size changes after
        // the first layout pass. Check usable controls rather than its exact
        // choice of client size.
        const auto contentsFit = [&] {
            if (!footerFits(*dialog))
                return false;
            if (!create)
                for (const char *name :
                     {"extractDestination", "extractPathName", "extractOverwriteMode",
                      "extractPassword", "extractShowPassword", "extractPathMode"})
                    if (auto *control = dialog->findChild<QWidget *>(name);
                        !control || !UiInspection::fullyVisible(*control, *dialog) ||
                        control->height() < control->minimumSizeHint().height())
                        return false;
            return true;
        };
        QTRY_VERIFY2(contentsFit(), qPrintable(UiInspection::dump(*dialog)));
        QVERIFY(UiInspection::writeReport(*dialog, tag));
        if (create) {
            auto *scroll = dialog->findChild<QScrollArea *>("archiveOptionsScrollArea");
            QVERIFY(scroll);
            auto *additional = dialog->findChild<QWidget *>("archiveAdditionalSection");
            auto *encryption = dialog->findChild<QGroupBox *>("archiveEncryptionGroup");
            QVERIFY(additional && encryption);
            auto *archivePath = dialog->findChild<QComboBox *>("archivePath");
            QVERIFY(archivePath);
            auto *folder = dialog->findChild<QLineEdit *>("archiveFolder");
            auto *caption = dialog->findChild<QLabel *>("archiveLabel");
            auto *browse = dialog->findChild<QPushButton *>("archiveBrowse");
            QVERIFY(folder && caption && browse);
            const QRect captionBounds = UiInspection::bounds(*caption, *dialog);
            const QRect pathBounds = UiInspection::bounds(*archivePath, *dialog);
            QVERIFY(captionBounds.right() < pathBounds.left());
            QVERIFY(UiInspection::fullyVisible(*folder, *dialog));
            archivePath->setFocus();
            QTest::keyClick(archivePath, Qt::Key_Tab);
            QTRY_VERIFY(folder->hasFocus());
            QTest::keyClick(folder, Qt::Key_Tab);
            QTRY_VERIFY(browse->hasFocus());
            QVERIFY(UiInspection::bounds(*encryption, *dialog).bottom() <
                    UiInspection::bounds(*additional, *dialog).top());
            for (const char *name :
                 {"archiveFormat", "compressionLevel", "archiveExpertOptions", "archivePassword",
                  "archivePasswordConfirmation", "archiveBrowse"}) {
                auto *control = dialog->findChild<QWidget *>(name);
                QVERIFY2(control, name);
                // ensureWidgetVisible can accept an already exposed input-method
                // cursor before applying margins. Probe the whole control instead.
                control->setFocus(Qt::TabFocusReason);
                if (scroll->widget()->isAncestorOf(control)) {
                    const QRect rect(control->mapTo(scroll->widget(), QPoint()), control->size());
                    scroll->ensureVisible(rect.center().x(), rect.center().y(),
                                          rect.width() / 2 + 16, rect.height() / 2 + 16);
                }
                QApplication::processEvents();
                QVERIFY(UiInspection::writeReport(*dialog, tag + "-" + name));
                QVERIFY2(
                    UiInspection::fullyVisible(*control, *dialog),
                    qPrintable(QString::fromLatin1(name) + "\n" + UiInspection::dump(*dialog)));
                QVERIFY(UiInspection::fullyVisible(*folder, *dialog));
                QVERIFY(UiInspection::fullyVisible(*caption, *dialog));
                QVERIFY(UiInspection::fullyVisible(*archivePath, *dialog));
            }
            for (const QString &format : {QString("7z"), QString("zip")}) {
                dialog->findChild<QComboBox *>("archiveFormat")->setCurrentText(format);
                QApplication::processEvents();
                QVERIFY2(footerFits(*dialog), qPrintable(UiInspection::dump(*dialog)));
                QVERIFY(UiInspection::bounds(*encryption, *dialog).bottom() <
                        UiInspection::bounds(*additional, *dialog).top());
                auto *methodLabel = dialog->findChild<QLabel *>("archiveEncryptionMethodLabel");
                auto *method = dialog->findChild<QComboBox *>("archiveEncryption");
                QVERIFY(methodLabel && method);
                if (method->isVisible()) {
                    const QRect labelBounds = UiInspection::bounds(*methodLabel, *dialog);
                    const QRect methodBounds = UiInspection::bounds(*method, *dialog);
                    QVERIFY(labelBounds.right() < methodBounds.left());
                    QVERIFY(qAbs(labelBounds.center().y() - methodBounds.center().y()) <
                            methodLabel->fontMetrics().height() / 2);
                }
                QVERIFY(EditArchiveExpert(*dialog, false, [&](QDialog &editor) {
                    editor.resize(requestedSize);
                    auto *memory = editor.findChild<QLabel *>("compressionDecompressionMemory");
                    QVERIFY(memory);
                    editor.findChild<QScrollArea *>("dialogContentScroll")
                        ->ensureWidgetVisible(memory);
                    QTRY_VERIFY(UiInspection::fullyVisible(*memory, editor));
                    QVERIFY(UiInspection::labelFits(*memory));
                    QVERIFY(UiInspection::writeReport(editor, tag + "-expert-" + format));
                }));
                QVERIFY(UiInspection::writeReport(*dialog, tag + "-" + format));
            }
        } else {
            auto *destination = dialog->findChild<QWidget *>("extractDestination");
            auto *browse = dialog->findChild<QWidget *>("extractBrowseButton");
            auto *pathMode = dialog->findChild<QWidget *>("extractPathMode");
            auto *overwrite = dialog->findChild<QWidget *>("extractOverwriteMode");
            auto *password = dialog->findChild<QWidget *>("extractPassword");
            QVERIFY(destination && browse && pathMode && overwrite && password);
            QVERIFY(UiInspection::separate(*destination, *browse, *dialog));
            QVERIFY(UiInspection::bounds(*destination, *dialog).bottom() <
                    UiInspection::bounds(*pathMode, *dialog).top());
            QVERIFY(UiInspection::bounds(*overwrite, *dialog).bottom() <
                    UiInspection::bounds(*password, *dialog).top());
            QVERIFY(UiInspection::bounds(*password, *dialog).bottom() <
                    UiInspection::bounds(*pathMode, *dialog).top());
            auto *nameEnabled = dialog->findChild<QCheckBox *>("extractNameEnable");
            auto *folderName = dialog->findChild<QLineEdit *>("extractPathName");
            QVERIFY(nameEnabled && folderName);
            nameEnabled->setChecked(true);
            QApplication::processEvents();
            const QString pendingName = folderName->text();
            const QSize windowSize = dialog->size();
            const QList<QWidget *> stableControls = {destination, browse,     pathMode,   overwrite,
                                                     password,    folderName, nameEnabled};
            QList<QRect> positions;
            for (const auto *control : stableControls)
                positions.append(UiInspection::bounds(*control, *dialog));
            for (bool enabled : {false, true, false}) {
                nameEnabled->setChecked(enabled);
                QApplication::processEvents();
                QVERIFY(folderName->isVisible());
                QCOMPARE(folderName->isEnabled(), enabled);
                QCOMPARE(folderName->text(), pendingName);
                QCOMPARE(dialog->size(), windowSize);
                for (int i = 0; i < stableControls.size(); ++i)
                    QCOMPARE(UiInspection::bounds(*stableControls[i], *dialog), positions[i]);
                QVERIFY2(footerFits(*dialog), qPrintable(UiInspection::dump(*dialog)));
                for (QWidget *control : {destination, browse, pathMode, overwrite, password})
                    QVERIFY2(UiInspection::fullyVisible(*control, *dialog),
                             qPrintable(UiInspection::dump(*dialog)));
                for (QLabel *label : dialog->findChildren<QLabel *>())
                    if (label->isVisible())
                        QVERIFY2(UiInspection::labelFits(*label), qPrintable(label->text()));
                QVERIFY(UiInspection::writeReport(*dialog, tag + (enabled ? "-name" : "-no-name")));
            }
        }
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(UiGeometryTests)
#include "ui_geometry_tests.moc"
