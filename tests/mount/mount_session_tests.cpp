#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/native_archive_fixture.h"
#include "mount/qtarchivemountsession.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class MountSessionTests final : public QObject {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
  private slots:
    void preparationStopsOnce_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("scenario");
        for (const QString &format : {QString("tar"), QString("zip")})
            for (int scenario = 0; scenario < 3; ++scenario)
                QTest::newRow(qPrintable(QString("%1-%2").arg(format).arg(scenario)))
                    << format << scenario;
    }
    void preparationStopsOnce() {
        QFETCH(QString, format);
        QFETCH(int, scenario);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("контейнер 空.") + format);
        NativeArchiveFixture::Item entry;
        entry.Path = QStringLiteral("папка space/данные.txt");
        entry.Bytes = "unchanged";
        QVERIFY(NativeArchiveFixture::Write(archive, {entry}, format));
        const QByteArray original = read(archive);
        QVERIFY(!original.isEmpty());
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(ArchiveBackend::openNativeArchive(NativeArchiveFixture::Catalog(), archive, format,
                                                   nullptr, reader, nullptr),
                 S_OK);
        auto *resource = new QtArchiveFileResource(archive);
        CMyComPtr<IUnknown> retained = resource;
        resource->AttachNativeOpen(reader);
        resource->UseNativeMetadata();
        QtArchiveMountSession session("cancel preparation");
        QSignalSpy stopped(&session, &QtArchiveMountSession::stopped);
        bool cancelledFromNotification = false;
        connect(&session, &QtArchiveMountSession::changed, this, [&] {
            if (scenario == 1 && session.status() == QtArchiveMountSession::Status::Preparing) {
                cancelledFromNotification = true;
                session.unmount();
            }
        });
        // Cancel before launch, or exercise asynchronous failure of the same
        // deliberately missing server without reaching an OS mount.
        session.start(retained, 1024, {}, fixture.filePath("missing SMB server"));
        if (scenario == 0)
            session.unmount();
        QTRY_COMPARE(session.status(), QtArchiveMountSession::Status::Stopped);
        QCOMPARE(cancelledFromNotification, scenario == 1);
        const QString error = session.error();
        QCOMPARE(error.isEmpty(), scenario != 2);
        session.unmount();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        QCOMPARE(session.error(), error);
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(session.status(), QtArchiveMountSession::Status::Stopped);
        QCOMPARE(read(archive), original);
    }
};

QTEST_GUILESS_MAIN(MountSessionTests)
#include "mount_session_tests.moc"
