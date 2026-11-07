#include "filemanager/views/filetable.h"
#include <QCoreApplication>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_MACOS
void PostNativeRegionalChange();
#endif

class RegionalSettingsTests final : public QObject {
    Q_OBJECT
  private slots:
    void systemNotificationRefreshesCachedPresentationWithoutLosingSelection() {
        FileTable view(3);
        FilePanelModel::Row row;
        row.sourceIndex = 0;
        row.name = QStringLiteral("selected й.txt");
        view.panelModel()->append(std::move(row));
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.selectRow(0);
        view.setCurrentCell(0, 0);
        const auto index = view.model()->index(0, 0);
        QVERIFY(view.selectionModel()->isRowSelected(0));
        const QString before = index.data().toString();
        QSignalSpy changed(view.model(), &QAbstractItemModel::dataChanged);
#ifdef Q_OS_WIN
        // WM_SETTINGCHANGE carries a pointer and cannot be posted asynchronously.
        SendMessageW(reinterpret_cast<HWND>(view.winId()), WM_SETTINGCHANGE, 0,
                     reinterpret_cast<LPARAM>(L"intl"));
#elif defined(Q_OS_MACOS)
        PostNativeRegionalChange();
#else
        QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(QEvent::LocaleChange));
#endif
        QTRY_VERIFY(!changed.isEmpty());
        bool tooltipsRefreshed = false;
        for (const auto &change : changed)
            tooltipsRefreshed |= change.at(2).value<QList<int>>().contains(Qt::ToolTipRole);
        QVERIFY(tooltipsRefreshed);
        QCOMPARE(index.data().toString(), before);
        QCOMPARE(view.currentIndex(), index);
        QVERIFY(view.selectionModel()->isRowSelected(0));
    }
};

QTEST_MAIN(RegionalSettingsTests)
#include "regional_settings_tests.moc"
