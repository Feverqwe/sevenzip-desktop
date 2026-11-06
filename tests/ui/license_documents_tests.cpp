#include "dialogs/licenses/qtlicensedocuments.h"
#include "platform/support/native_link_fixture.h"
#include "ui/support/license_documents_fixture.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace LicenseDocumentsFixture;

class LicenseDocumentsTests final : public QObject {
    Q_OBJECT
  private slots:
    void fullTextAndIndependentCatalogs() {
        QTemporaryDir first, second;
        QVERIFY(first.isValid() && second.isValid());
        const QString name = QStringLiteral("компонент 空/LICENSE.txt");
        const QString text =
            QStringLiteral("Original <b>text</b> δ https://example.invalid/\n").repeated(1000);
        QVERIFY(Write(first.filePath(name), text.toUtf8()));
        QVERIFY(Write(second.filePath(name), "other distribution\n"));
        QVERIFY(Index(first.path(), {name}));
        QVERIFY(Index(second.path(), {name}));
        QtLicenseDocuments a(first.path()), b(second.path());
        QString error;
        QVERIFY(a.load(error));
        QVERIFY(b.load(error));
        QCOMPARE(a.read(0, error), std::optional<QString>(text));
        QVERIFY(error.isEmpty());
        QCOMPARE(b.read(0, error), std::optional<QString>("other distribution\n"));
        QVERIFY(a.load(error));
        QCOMPARE(a.read(0, error), std::optional<QString>(text));
    }
    void unavailableDocumentDoesNotReplaceOthers() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(Index(fixture.path(), {"good.txt", "missing.txt", "bad.txt"}));
        QVERIFY(Write(fixture.filePath("good.txt"), "original\n"));
        QVERIFY(Write(fixture.filePath("bad.txt"), QByteArray("invalid\xff", 8)));
        QtLicenseDocuments documents(fixture.path());
        QString error;
        QVERIFY(documents.load(error));
        QVERIFY(!documents.read(1, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!documents.read(2, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(documents.read(0, error), std::optional<QString>("original\n"));
        QVERIFY(error.isEmpty());
        QVERIFY(!documents.read(-1, error));
        QVERIFY(!documents.read(3, error));
    }
    void indexedPathsStayInsideInstalledDirectory() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("Notices");
        QVERIFY(QDir().mkpath(root));
        const QString outside = fixture.filePath("outside.txt");
        QVERIFY(Write(outside, "outside bytes"));
        QVERIFY(Index(root, {"../outside.txt", outside, "bad\\path.txt", "good.txt"}));
        QVERIFY(Write(QDir(root).filePath("good.txt"), "original"));
        QtLicenseDocuments documents(root);
        QString error;
        QVERIFY(documents.load(error));
        for (int row = 0; row < 3; ++row) {
            QVERIFY(!documents.read(row, error));
            QVERIFY(!error.isEmpty());
        }
        QCOMPARE(documents.read(3, error), std::optional<QString>("original"));
        QVERIFY(CreateNativeSymbolicLink(QDir(root).filePath("escape.txt"), outside));
        QVERIFY(Index(root, {"escape.txt", "good.txt"}));
        QVERIFY(documents.load(error));
        QVERIFY(!documents.read(0, error));
        QCOMPARE(documents.read(1, error), std::optional<QString>("original"));
    }
    void missingEmptyAndCorruptIndexesRemainDistinct() {
        QTemporaryDir fixture;
        QtLicenseDocuments documents(fixture.path());
        QString error;
        QVERIFY(!documents.load(error));
        QVERIFY(!error.isEmpty());
        QVERIFY(Index(fixture.path(), {}));
        QVERIFY(documents.load(error));
        QVERIFY(error.isEmpty());
        QVERIFY(documents.documents().isEmpty());
        QVERIFY(Write(fixture.filePath("index.json"), "not json"));
        QVERIFY(!documents.load(error));
        QVERIFY(!error.isEmpty());
        QVERIFY(documents.documents().isEmpty());
    }
};
QTEST_GUILESS_MAIN(LicenseDocumentsTests)
#include "license_documents_tests.moc"
