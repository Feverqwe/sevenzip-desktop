#include "Common/MyCom.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QtTest>

class ArchivePasswordTests final : public QObject {
    Q_OBJECT
  private slots:
    void unicodeCallbackAndRetry() {
        // Exercise both native wchar_t widths and length-aware password copies.
        QString first = QString::fromUtf8("  пароль 🔐 密碼  ");
        first.append(QChar(0));
        first.append(QStringLiteral("tail"));
        ArchivePassword opening(first);
        ArchivePassword retained = opening;
        opening.clear();
        ArchivePassword nativeCopy(retained.native());
        QtArchiveReadOperation operation({}, nativeCopy.toQString(), {},
                                         QtArchiveReadOperation::Kind::Test);
        nativeCopy.clear();
        retained.clear();
        CMyComBSTR_Wipe result;
        QCOMPARE(operation.password(&result), S_OK);
        QCOMPARE(QString::fromWCharArray(result, SysStringLen(*&result)), first);
        result.Wipe_and_Free();

        const QString second = QString::fromUtf8("другой 🗝 пароль");
        int requested = 0;
        operation.PasswordRequired = [&] {
            ++requested;
            operation.providePassword(second);
        };
        QCOMPARE(operation.resetPassword(), S_OK);
        QCOMPARE(operation.password(&result), S_OK);
        QCOMPARE(requested, 1);
        QCOMPARE(QString::fromWCharArray(result, SysStringLen(*&result)), second);
        result.Wipe_and_Free();

        operation.PasswordRequired = [&] {
            ++requested;
            operation.providePassword({});
        };
        QCOMPARE(operation.resetPassword(), S_OK);
        QCOMPARE(operation.password(&result), S_OK);
        QCOMPARE(SysStringLen(*&result), 0u);
        QCOMPARE(requested, 2);
        result.Wipe_and_Free();
        QCOMPARE(operation.password(&result), S_OK);
        QCOMPARE(requested, 2);
        result.Wipe_and_Free();

        QCOMPARE(operation.resetPassword(), S_OK);
        operation.cancel();
        QCOMPARE(operation.password(&result), E_ABORT);
        QVERIFY(result == nullptr);
        QCOMPARE(requested, 2);
    }
};
QTEST_GUILESS_MAIN(ArchivePasswordTests)
#include "archive_password_tests.moc"
