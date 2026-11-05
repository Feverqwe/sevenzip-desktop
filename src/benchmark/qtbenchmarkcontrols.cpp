#include "benchmark/qtbenchmarkcontrols.h"
#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtmodalresult.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>

CQtBenchmarkWindow::CQtBenchmarkWindow(QWidget *parent) : QDialog(parent) {
    QtTaskPresentation::Watch(*this, QtOperationPhase::Working);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    clock_.start();
    QtBindResourceText(this, IDD_BENCH, ResourceCaption(IDD_BENCH));
    setObjectName("IDD_BENCH");
}
void CQtBenchmarkWindow::UpdateWrappedWidths(int width) {
    const int available = qMax(1, width - style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    for (unsigned id : {IDT_BENCH_ERROR_MESSAGE, IDT_BENCH_CPU, IDT_BENCH_CPU_FEATURE,
                        IDT_BENCH_VER, IDT_BENCH_SYS1, IDT_BENCH_SYS2, IDT_BENCH_LOG})
        if (auto *view = GetItem(id)) {
            view->setMaximumWidth(available);
            if (id != IDT_BENCH_CPU_FEATURE && id != IDT_BENCH_VER)
                view->setMinimumWidth(available);
        }
}
bool CQtBenchmarkWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::Resize) {
        if (auto *label = qobject_cast<QLabel *>(object); label && label->wordWrap()) {
            // QGridLayout can calculate a spanning label's height using the
            // wider grid, even when its actual width is constrained.
            label->setMinimumHeight(
                label->heightForWidth(static_cast<QResizeEvent *>(event)->size().width()));
        }
        auto *scroll = findChild<QScrollArea *>("benchmarkControlsScroll");
        if (scroll && object == scroll->viewport())
            UpdateWrappedWidths(static_cast<QResizeEvent *>(event)->size().width());
    }
    return QDialog::eventFilter(object, event);
}
void CQtBenchmarkWindow::SetItemText(unsigned id, const UString &text) {
    const QString value = QString::fromWCharArray(text.Ptr());
    if (auto *label = qobject_cast<QLabel *>(GetItem(id))) {
        label->setText(value);
        if (label->wordWrap())
            label->setMinimumHeight(label->heightForWidth(label->width()));
        if (id == IDT_BENCH_LOG || id == IDT_BENCH_SYS1 || id == IDT_BENCH_SYS2)
            label->setVisible(!value.isEmpty());
    } else if (auto *edit = qobject_cast<QPlainTextEdit *>(GetItem(id))) {
        edit->setPlainText(value);
        if (id == IDT_BENCH_LOG)
            edit->setVisible(!value.isEmpty());
    }
}
void CQtBenchmarkWindow::SetItemTextA(unsigned id, const AString &text) {
    SetItemText(id, UString(QString::fromUtf8(text.Ptr()).toStdWString().c_str()));
}
void CQtBenchmarkWindow::GetItemText(unsigned id, UString &text) const {
    if (const auto *label = qobject_cast<QLabel *>(GetItem(id)))
        text = label->text().toStdWString().c_str();
}
void CQtBenchmarkWindow::EnableItem(unsigned id, bool enabled) {
    if (auto *view = GetItem(id))
        view->setEnabled(enabled);
}
QWidget *CQtBenchmarkWindow::GetFocus() const { return QApplication::focusWidget(); }
UInt32 CQtBenchmarkWindow::GetTickCount() const { return UInt32(clock_.elapsed()); }
void CQtBenchmarkWindow::NormalizeSize(bool fullNormalize) {
    QtNormalizeDialogSize(*this, fullNormalize);
}
void CQtBenchmarkWindow::NormalizePosition() { QtNormalizeDialogPosition(*this); }
void CQtBenchmarkWindow::SendMsg_NextDlgCtl_CtlId(unsigned id) {
    if (auto *view = GetItem(id))
        view->setFocus();
}
quintptr CQtBenchmarkWindow::SetTimer(quintptr id, unsigned milliseconds) {
    timer_.stop();
    disconnect(&timer_, nullptr, this, nullptr);
    connect(&timer_, &QTimer::timeout, this, [this, id] { OnTimer(id); });
    timer_.start(int(milliseconds));
    return id;
}
void CQtBenchmarkWindow::KillTimer(quintptr) { timer_.stop(); }
bool CQtBenchmarkWindow::PostMsg(unsigned message, quintptr wparam) {
    // Original PostMsg_Finish succeeds without posting after Attach(NULL).
    if (!postsEnabled_)
        return true;
    return QMetaObject::invokeMethod(
        this, [this, message, wparam] { OnMessage(message, wparam); }, Qt::QueuedConnection);
}
qintptr CQtBenchmarkWindow::Create(unsigned resource, QWidget *parent) {
    if (parent)
        setParent(parent, windowFlags());
    BuildControls(resource == IDD_BENCH_TOTAL);
    OnInit();
    auto *copy = findChild<QPushButton *>("copyReportButton");
    if (resource == IDD_BENCH_TOTAL) {
        QWidget::setTabOrder(GetItem(IDE_BENCH2_EDIT), copy);
        QWidget::setTabOrder(copy, GetItem(IDHELP));
        QWidget::setTabOrder(GetItem(IDHELP), GetItem(IDCANCEL));
        GetItem(IDE_BENCH2_EDIT)->setFocus();
    } else {
        const unsigned order[] = {IDC_BENCH_DICTIONARY,
                                  IDC_BENCH_NUM_THREADS,
                                  IDC_BENCH_NUM_PASSES,
                                  IDT_BENCH_LOG,
                                  IDB_RESTART,
                                  IDB_STOP,
                                  IDHELP,
                                  IDCANCEL};
        for (unsigned i = 1; i < sizeof(order) / sizeof(order[0]); ++i)
            QWidget::setTabOrder(GetItem(order[i - 1]), GetItem(order[i]));
        QWidget::setTabOrder(GetItem(IDT_BENCH_LOG), copy);
        QWidget::setTabOrder(copy, GetItem(IDB_RESTART));
        auto *scroll = findChild<QScrollArea *>("benchmarkControlsScroll");
        QWidget::setTabOrder(GetItem(IDC_BENCH_NUM_PASSES), scroll);
        QWidget::setTabOrder(scroll, GetItem(IDT_BENCH_LOG));
        static_cast<QPushButton *>(GetItem(IDB_RESTART))->setDefault(true);
        GetItem(IDB_RESTART)->setFocus();
    }
    const int result = exec();
    OnDestroy();
    return result;
}
void CQtBenchmarkWindow::OnCancel() { QDialog::reject(); }
void CQtBenchmarkWindow::reject() { OnCancel(); }
void CQtBenchmarkWindow::closeEvent(QCloseEvent *event) {
    event->ignore();
    OnCancel();
}
