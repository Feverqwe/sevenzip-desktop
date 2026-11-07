#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/providers/FSFolder.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class FilesystemCommentTests final : public QObject {
    Q_OBJECT

  private slots:
    void readsExistingDescriptionsWithoutRewriting() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("имя 空 with spaces.bin");
        QFile item(fixture.filePath(name));
        QVERIFY(item.open(QIODevice::WriteOnly));
        item.close();
        const QByteArray descriptions =
            QByteArray("\xef\xbb\xbf\r\n") +
            (QLatin1Char('"') + name + QStringLiteral("\" Комментарий 空\r\n")).toUtf8();
        QFile file(fixture.filePath("descript.ion"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(descriptions), qint64(descriptions.size()));
        file.close();

        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        bool found = false;
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant value;
            QCOMPARE(folder->GetProperty(i, kpidName, &value), S_OK);
            if (value.vt != VT_BSTR || QString::fromWCharArray(value.bstrVal) != name)
                continue;
            QCOMPARE(folder->GetProperty(i, kpidComment, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_BSTR));
            QCOMPARE(QString::fromWCharArray(value.bstrVal), QStringLiteral("Комментарий 空"));
            found = true;
            break;
        }
        QVERIFY(found);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), descriptions);
    }
};

QTEST_GUILESS_MAIN(FilesystemCommentTests)
#include "filesystem_comment_tests.moc"
