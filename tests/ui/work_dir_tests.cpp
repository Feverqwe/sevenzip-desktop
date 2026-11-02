#include "Common/StringConvert.h"
#include "platform/temp/WorkDir.h"
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {
FString nativePath(const QString &path) {
    const auto value = QDir::toNativeSeparators(path).toStdWString();
    return us2fs(value.c_str());
}
QString qtPath(const FString &path) {
    return QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(path).Ptr()));
}
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class WorkDirTests final : public QObject {
    Q_OBJECT
  private slots:
    void settingsDefaultsAndModes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("settings.ini");
        NWorkDir::CInfo info;
        info.Load(settings);
        QCOMPARE(info.Mode, NWorkDir::NMode::kSystem);
        QVERIFY(info.ForRemovableOnly);
        info.Mode = NWorkDir::NMode::kSpecified;
        info.ForRemovableOnly = false;
        info.Path = nativePath(fixture.filePath("рабочая папка"));
        info.Save(settings);
        NWorkDir::CInfo loaded;
        loaded.Load(settings);
        QCOMPARE(loaded.Mode, info.Mode);
        QCOMPARE(qtPath(loaded.Path), qtPath(info.Path));
        QVERIFY(!loaded.ForRemovableOnly);
        FString name;
        QCOMPARE(qtPath(GetWorkDir(loaded, nativePath(fixture.filePath("архив.zip")), name)),
                 qtPath(info.Path) + '/');
        QCOMPARE(qtPath(name), QStringLiteral("архив.zip"));
        {
            QSettings key(settings, QSettings::IniFormat);
            key.remove("Options/WorkDirPath");
        }
        loaded.Load(settings);
        QCOMPARE(loaded.Mode, NWorkDir::NMode::kSystem);
        info.Mode = NWorkDir::NMode::kCurrent;
        QCOMPARE(qtPath(GetWorkDir(info, nativePath(fixture.filePath("архив.zip")), name)),
                 fixture.path() + '/');
    }

    void publishAndAbandonedTemporaryFile() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("settings.ini");
        const QString original = fixture.filePath("архив с пробелом.zip");
        QFile existing(original);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("original"), qint64(8));
        existing.close();
        NWorkDir::CInfo info;
        info.SetDefault();
        info.Mode = NWorkDir::NMode::kSpecified;
        info.ForRemovableOnly = false;
        info.Path = nativePath(fixture.filePath("work/новая папка"));
        info.Save(settings);
        QString abandoned;
        {
            CWorkDirTempFile temp;
            QCOMPARE(temp.CreateTempFile(nativePath(original), settings), S_OK);
            abandoned = qtPath(temp.Get_TempFilePath());
            QVERIFY(QFile::exists(abandoned));
            QCOMPARE(contents(original), QByteArray("original"));
            UInt32 written = 0;
            QCOMPARE(temp.OutStream->Write("discard", 7, &written), S_OK);
            QCOMPARE(written, UInt32(7));
        }
        QVERIFY(!QFile::exists(abandoned));
        QCOMPARE(contents(original), QByteArray("original"));
        QString published;
        {
            CWorkDirTempFile temp;
            QCOMPARE(temp.CreateTempFile(nativePath(original), settings), S_OK);
            published = qtPath(temp.Get_TempFilePath());
            UInt32 written = 0;
            QCOMPARE(temp.OutStream->Write("updated", 7, &written), S_OK);
            QCOMPARE(written, UInt32(7));
            QCOMPARE(temp.MoveToOriginal(true), S_OK);
        }
        QCOMPARE(contents(original), QByteArray("updated"));
        QVERIFY(!QFile::exists(published));
    }

    void invalidWorkingDirectoryPreservesOriginal() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = fixture.filePath("original.zip");
        QFile file(original);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        NWorkDir::CInfo info;
        info.SetDefault();
        info.Mode = NWorkDir::NMode::kSpecified;
        info.ForRemovableOnly = false;
        info.Path = nativePath(original + "/blocked");
        const QString settings = fixture.filePath("settings.ini");
        info.Save(settings);
        CWorkDirTempFile temp;
        QVERIFY(temp.CreateTempFile(nativePath(original), settings) != S_OK);
        QCOMPARE(contents(original), QByteArray("original"));
    }

#ifdef Q_OS_LINUX
    void publishedArchiveSurvivesTemporaryCleanupFailure() {
        if (!QFileInfo("/dev/shm").isDir() ||
            QStorageInfo("/dev/shm").device() == QStorageInfo(QDir::tempPath()).device())
            QSKIP("No separate /dev/shm filesystem for cross-volume publication");
        QTemporaryDir fixture;
        QTemporaryDir work("/dev/shm/sevenzip-publication-XXXXXX");
        QVERIFY(fixture.isValid() && work.isValid());
        const QString original = fixture.filePath("archive 空.zip");
        QFile file(original);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        NWorkDir::CInfo info;
        info.SetDefault();
        info.Mode = NWorkDir::NMode::kSpecified;
        info.ForRemovableOnly = false;
        info.Path = nativePath(work.path());
        const QString settings = fixture.filePath("settings.ini");
        info.Save(settings);
        CWorkDirTempFile temp;
        QCOMPARE(temp.CreateTempFile(nativePath(original), settings), S_OK);
        const QString temporary = qtPath(temp.Get_TempFilePath());
        const auto permissions = QFile::permissions(work.path());
        const auto cleanup = qScopeGuard([&] {
            QFile::setPermissions(work.path(), permissions);
            QFile::remove(temporary);
        });
        UInt32 written = 0;
        QCOMPARE(temp.OutStream->Write("published", 9, &written), S_OK);
        QCOMPARE(written, UInt32(9));
        struct DenyCleanup final : NWindows::NFile::NDir::ICopyFileProgress {
            QString directory;
            bool denied = false;
            DWORD CopyFileProgress(UInt64, UInt64) override {
                denied = QFile::setPermissions(directory,
                                               QFileDevice::ReadOwner | QFileDevice::ExeOwner);
                return PROGRESS_CONTINUE;
            }
        } progress;
        progress.directory = work.path();
        bool published = false;
        const HRESULT result = temp.MoveToOriginal(true, &progress, &published);
        QVERIFY(progress.denied);
        QVERIFY(result != S_OK);
        QVERIFY(published);
        QCOMPARE(contents(original), QByteArray("published"));
        QCOMPARE(contents(temporary), QByteArray("published"));
    }
#endif

    void temporaryDirectoryCleanupDoesNotFollowLinks() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString outside = fixture.filePath("outside");
        QVERIFY(QDir().mkdir(outside));
        QFile file(outside + "/keep");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep"), qint64(4));
        file.close();
        QString temporary;
        {
            NWindows::NFile::NDir::CTempDir dir;
            QVERIFY(dir.Create(nativePath("sevenzip-work-dir-test")));
            temporary = qtPath(dir.GetPath());
#ifndef Q_OS_WIN
            QVERIFY(QFile::link(outside, temporary + "/link"));
#endif
            QVERIFY(QDir().mkpath(temporary + "/nested/юникод"));
        }
        QVERIFY(!QFile::exists(temporary));
        QCOMPARE(contents(outside + "/keep"), QByteArray("keep"));
    }
};

QTEST_GUILESS_MAIN(WorkDirTests)
#include "work_dir_tests.moc"
