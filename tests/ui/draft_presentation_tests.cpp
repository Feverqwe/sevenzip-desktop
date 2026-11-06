#include "ui/lifecycle/qttaskpresentation.h"
#include <QDialog>
#include <QLineEdit>
#include <QtTest>

class DraftPresentationTests final : public QObject {
    Q_OBJECT
  private slots:
    void questionsDeferAutomaticPresentation() {
        QDialog draft;
        QtTaskPresentation::Watch(draft, QtOperationPhase::Draft);
        draft.show();
        QTRY_VERIFY(draft.isVisible());
        QVERIFY(QtTaskPresentation::CanPresent());
        {
            const QtTaskPresentation::QuestionScope nativeQuestion;
            QVERIFY(!QtTaskPresentation::CanPresent());
            {
                const QtTaskPresentation::QuestionScope nested;
                QVERIFY(!QtTaskPresentation::CanPresent());
            }
            QVERIFY(!QtTaskPresentation::CanPresent());
        }
        QVERIFY(QtTaskPresentation::CanPresent());
        QDialog question(&draft);
        question.setWindowModality(Qt::WindowModal);
        question.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &question);
        QVERIFY(!QtTaskPresentation::CanPresent());
        question.reject();
        QTRY_VERIFY(QtTaskPresentation::CanPresent());
    }
    void editingDefersAutomaticPresentation() {
        QDialog draft;
        QLineEdit input(&draft);
        input.setText(QStringLiteral("unapplied ввод"));
        draft.show();
        draft.activateWindow();
        input.setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), &input);
        QVERIFY(!QtTaskPresentation::CanPresent());
        QCOMPARE(input.text(), QStringLiteral("unapplied ввод"));
        input.clearFocus();
        QTRY_VERIFY(QtTaskPresentation::CanPresent());
    }
};
QTEST_MAIN(DraftPresentationTests)
#include "draft_presentation_tests.moc"
