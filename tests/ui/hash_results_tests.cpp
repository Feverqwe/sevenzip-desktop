#include "7zip/UI/FileManager/resource.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "gui/HashGUI.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

class HashResultsTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeBundleResultsAndModalLayout() {
        CHashBundle hash;
        UStringVector methods;
        methods.Add(L"CRC32");
        QCOMPARE(hash.SetMethods(nullptr, methods), S_OK);
        hash.FirstFileName = L"имя с пробелом.bin";
        hash.InitForNewFile();
        hash.Update("123456789", 9);
        hash.SetSize(9);
        hash.Final(false, false, hash.FirstFileName);
        CPropNameValPairs pairs;
        AddHashBundleRes(pairs, hash);
        QVERIFY(!pairs.IsEmpty());
        bool foundDigest = false;
        FOR_VECTOR(i, pairs)
        if (pairs[i].Value.IsEqualTo("CBF43926"))
            foundDigest = true;
        QVERIFY(foundDigest);
        for (int width : {540, 420}) {
            bool inspected = false, valid = false, report = false;
            QTimer::singleShot(0, qApp, [&] {
                auto *dialog =
                    qobject_cast<QtTaskResultDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                inspected = true;
                dialog->resize(width, 360);
                QApplication::processEvents();
                auto *text = dialog->findChild<QPlainTextEdit *>("taskResultReport");
                auto *buttons = dialog->findChild<QDialogButtonBox *>("taskResultActions");
                valid = text && buttons && text->isReadOnly() &&
                        text->toPlainText().contains("CBF43926") &&
                        text->toPlainText().contains(QString::fromWCharArray(hash.FirstFileName)) &&
                        UiInspection::fullyVisible(*text, *dialog) &&
                        UiInspection::fullyVisible(*buttons, *dialog) &&
                        UiInspection::separate(*text, *buttons, *dialog);
                report = UiInspection::writeReport(*dialog, QString("hash-results-%1").arg(width));
                dialog->reject();
            });
            ShowHashResults(pairs, nullptr);
            QVERIFY(inspected);
            QVERIFY(valid);
            QVERIFY(report);
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(HashResultsTests)
#include "hash_results_tests.moc"
