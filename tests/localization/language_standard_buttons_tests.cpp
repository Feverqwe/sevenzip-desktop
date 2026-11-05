#include "dialogs/about/AboutDialog.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "dialogs/combo/ComboDialog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "dialogs/copy/CopyDialog.h"
#include "dialogs/edit/EditDialog.h"
#include "dialogs/extract/ExtractDialog.h"
#include "dialogs/listview/ListViewDialog.h"
#include "dialogs/mem/MemDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "dialogs/split/SplitDialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>
#include <memory>

class LanguageStandardButtonsTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    NCompression::CFormatOptions profile_;
    CCompressDialog compression_;
    void language(bool russian) {
        const QString path = settings_.filePath("language.ini");
        SaveRegLang(UString(russian ? "ru" : "-"), path);
        ReloadLang(path);
        QtNotifyLanguageChanged();
        QApplication::processEvents();
    }
    template <class T> static std::unique_ptr<QDialog> initialize() {
        auto dialog = std::make_unique<T>();
        if (!dialog->OnInit())
            return {};
        return dialog;
    }
    std::unique_ptr<QDialog> create(const QString &name) {
        if (name == "copy")
            return std::make_unique<CCopyDialog>(false, "/pending 空 path", QStringList{"file"},
                                                 QStringList{});
        if (name == "split")
            return std::make_unique<CSplitDialog>("file 空.bin", "/pending path");
        if (name == "combo")
            return std::make_unique<CComboDialog>("Title", "Name", "pending 空 value");
        if (name == "password")
            return initialize<CPasswordDialog>();
        if (name == "memory")
            return initialize<CMemDialog>();
        if (name == "edit")
            return initialize<CEditDialog>();
        if (name == "list")
            return initialize<CListViewDialog>();
        if (name == "about")
            return initialize<CAboutDialog>();
        if (name == "browse")
            return std::make_unique<CBrowseDialog2>();
        if (name == "compress-options") {
            compression_.FormatOptions = &profile_;
            compression_.ArcInfo.Name = "7z";
            compression_.ArcInfo.Flags = NArcInfoFlags::kCTime | NArcInfoFlags::kATime |
                                         NArcInfoFlags::kMTime | NArcInfoFlags::kMTime_Default;
            compression_.ArcInfo.TimeFlags = TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kWindows);
            return std::make_unique<COptionsDialog>(&compression_);
        }
        if (name == "compress")
            return std::make_unique<CreateArchiveDialog>("/pending 空 path", QStringList{"file"});
        return std::make_unique<CExtractDialog>("/pending 空 path", QStringList{});
    }
    static QString expected(QDialogButtonBox::StandardButton button, bool russian, bool memory) {
        if (button == QDialogButtonBox::Ok)
            return memory ? (russian ? QStringLiteral("&Продолжить") : QString("Continue"))
                          : QString("OK");
        if (button == QDialogButtonBox::Cancel)
            return russian ? QStringLiteral("Отмена") : QString("Cancel");
        if (button == QDialogButtonBox::Close)
            return russian ? QStringLiteral("&Закрыть") : QString("&Close");
        return russian ? QStringLiteral("Помощь") : QString("Help");
    }
    void verifyButtons(QDialog &dialog, const QString &name, bool russian) {
        auto *box = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(box);
        for (auto button : {QDialogButtonBox::Ok, QDialogButtonBox::Cancel, QDialogButtonBox::Close,
                            QDialogButtonBox::Help})
            if (auto *control = box->button(button)) {
                if (button == QDialogButtonBox::Ok && name == "compress")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Создать архив") : QString("Create archive"));
                else if (button == QDialogButtonBox::Ok && name == "extract")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Распаковать") : QString("Extract"));
                else if (button == QDialogButtonBox::Ok && name == "compress-options")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Сохранить") : QString("Save"));
                else if (button == QDialogButtonBox::Ok && name == "list")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Открыть") : QString("Open"));
                else if (button == QDialogButtonBox::Ok && name == "copy")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Копировать") : QString("Copy"));
                else if (button == QDialogButtonBox::Ok && name == "split")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Разделить") : QString("Split"));
                else if (button == QDialogButtonBox::Ok && name == "password")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Продолжить") : QString("Continue"));
                else if (button == QDialogButtonBox::Ok && name == "about")
                    QCOMPARE(control->text(),
                             russian ? QStringLiteral("Закрыть") : QString("Close"));
                else
                    QCOMPARE(control->text(), expected(button, russian, name == "memory"));
            }
    }
    void inspect(QDialog &dialog, const QString &name, bool russian) {
        QSize normal(640, 480), compact(480, 400);
        if (name == "copy") {
            normal = QSize(440, 250);
            compact = QSize(340, 220);
        } else if (name == "split") {
            normal = QSize(480, 240);
            compact = QSize(340, 240);
        } else if (name == "password" || name == "combo") {
            normal = QSize(360, 170);
            compact = QSize(260, 170);
        } else if (name == "memory") {
            normal = QSize(700, 460);
            compact = QSize(560, 460);
        } else if (name == "edit") {
            normal = QSize(480, 320);
            compact = QSize(320, 240);
        } else if (name == "list") {
            normal = QSize(540, 360);
            compact = QSize(360, 240);
        } else if (name == "about") {
            normal = QSize(360, 330);
            compact = QSize(300, 310);
        } else if (name == "extract") {
            normal = QSize(640, 360);
            compact = QSize(470, 330);
        } else if (name == "browse") {
            normal = QSize(900, 580);
            compact = QSize(500, 320);
        } else if (name == "compress-options") {
            normal = QSize(520, 400);
            compact = QSize(440, 340);
        } else if (name == "compress") {
            normal = QSize(760, 720);
            compact = QSize(640, 480);
        }
        for (const QSize &size : {normal, compact}) {
            dialog.resize(size.expandedTo(dialog.minimumSizeHint()));
            dialog.show();
            QApplication::processEvents();
            if (name == "compress-options") {
                dialog.resize(dialog.width(), dialog.minimumSizeHint().height());
                QApplication::processEvents();
            }
            auto *box = dialog.findChild<QDialogButtonBox *>();
            QVERIFY(box);
            QVERIFY(UiInspection::fullyVisible(*box, dialog));
            for (auto *button : box->findChildren<QPushButton *>())
                if (button->isVisible()) {
                    QVERIFY(UiInspection::fullyVisible(*button, dialog));
                    QVERIFY(button->width() >= button->sizeHint().width());
                }
            QVERIFY(UiInspection::writeReport(dialog, QString("standard-buttons-%1-%2-%3")
                                                          .arg(name, russian ? "ru" : "en")
                                                          .arg(size.width())));
        }
    }

  private slots:
    void cleanup() { language(false); }
    void fontChangeKeepsActionCaptionAndFits() {
        QDialog dialog;
        auto *layout = new QVBoxLayout(&dialog);
        auto *buttons =
            new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        auto *cancel = buttons->button(QDialogButtonBox::Cancel);
        const QString caption = QStringLiteral("Keep pending changes");
        cancel->setText(caption);
        for (int points : {24, 12}) {
            auto font = buttons->font();
            font.setPointSize(points);
            buttons->setFont(font);
            dialog.adjustSize();
            dialog.show();
            QApplication::processEvents();
            QCOMPARE(cancel->text(), caption);
            QVERIFY(cancel->width() >= cancel->fontMetrics().horizontalAdvance(caption));
        }
    }
    void allStandardMappingsKeepResourceFallbackOnReload() {
        language(false);
        QDialog dialog;
        QtLangButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                    QDialogButtonBox::Yes | QDialogButtonBox::No |
                                    QDialogButtonBox::Close | QDialogButtonBox::Help,
                                &dialog);
        for (bool russian : {true, false, true}) {
            language(russian);
            QCOMPARE(buttons.button(QDialogButtonBox::Yes)->text(),
                     russian ? QStringLiteral("&Да") : QStringLiteral("&Yes"));
            QCOMPARE(buttons.button(QDialogButtonBox::No)->text(),
                     russian ? QStringLiteral("&Нет") : QStringLiteral("&No"));
            verifyButtons(dialog, "standard", russian);
        }
        SaveRegLang(UString("missing-language-fixture"), settings_.filePath("language.ini"));
        ReloadLang(settings_.filePath("language.ini"));
        QtNotifyLanguageChanged();
        QApplication::processEvents();
        verifyButtons(dialog, "standard", false);
    }
    void sourceDialogsTranslateTheirStandardControls_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("russian");
        for (const char *name : {"copy", "split", "combo", "password", "memory", "edit", "list",
                                 "about", "extract", "browse", "compress-options", "compress"})
            for (bool russian : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(name).arg(russian)))
                    << QString::fromLatin1(name) << russian;
    }
    void sourceDialogsTranslateTheirStandardControls() {
        QFETCH(QString, name);
        QFETCH(bool, russian);
        language(russian);
        auto dialog = create(name);
        QVERIFY(dialog);
        QSignalSpy accepted(dialog.get(), &QDialog::accepted);
        QSignalSpy rejected(dialog.get(), &QDialog::rejected);
        verifyButtons(*dialog, name, russian);
        inspect(*dialog, name, russian);
        language(!russian);
        verifyButtons(*dialog, name, !russian);
        inspect(*dialog, name, !russian);
        language(russian);
        verifyButtons(*dialog, name, russian);
        QCOMPARE(accepted.size(), 0);
        QCOMPARE(rejected.size(), 0);
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LanguageStandardButtonsTests)
#include "language_standard_buttons_tests.moc"
