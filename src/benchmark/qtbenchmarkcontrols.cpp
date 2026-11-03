#include "benchmark/qtbenchmarkcontrols.h"
#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtmodalresult.h"
#include "ui/reports/qttextreport.h"
#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>

QString CQtBenchmarkWindow::ResourceCaption(unsigned id) {
    // Original BenchmarkDialog.rc captions are not STRINGTABLE entries.
    static const QHash<unsigned, QString> captions = {
        {IDD_BENCH, "Benchmark"},
        {IDT_BENCH_DICTIONARY, "&Dictionary size:"},
        {IDT_BENCH_MEMORY, "Memory usage:"},
        {IDT_BENCH_NUM_THREADS, "&Number of CPU threads:"},
        {IDT_BENCH_SIZE, "Size"},
        {IDT_BENCH_SPEED, "Speed"},
        {IDT_BENCH_USAGE_LABEL, "CPU Usage"},
        {IDT_BENCH_RPU_LABEL, "Rating / Usage"},
        {IDT_BENCH_RATING_LABEL, "Rating"},
        {IDG_BENCH_COMPRESSING, "Compressing"},
        {IDG_BENCH_DECOMPRESSING, "Decompressing"},
        {IDG_BENCH_TOTAL_RATING, "Total Rating"},
        {IDT_BENCH_CURRENT, "Current"},
        {IDT_BENCH_RESULTING, "Resulting"},
        {IDT_BENCH_ELAPSED, "Elapsed time:"},
        {IDT_BENCH_PASSES, "Passes:"},
        {IDB_RESTART, "&Restart"},
        {IDB_STOP, "&Stop"}};
    return captions.value(id);
}
QString CQtBenchmarkWindow::QtLangText(unsigned id) {
    return ::QtLangText(id, ResourceCaption(id));
}

CQtBenchmarkWindow::CQtBenchmarkWindow(QWidget *parent) : QDialog(parent) {
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    clock_.start();
    QtBindResourceText(this, IDD_BENCH, ResourceCaption(IDD_BENCH));
    setObjectName("IDD_BENCH");
}
QWidget *CQtBenchmarkWindow::Register(unsigned id, const char *name, QWidget *widget) {
    controls_.insert(id, widget);
    widget->setObjectName(QString::fromLatin1(name));
    unsigned captionId = id;
    if (id == IDT_BENCH_CURRENT + 50 || id == IDT_BENCH_RESULTING + 50)
        captionId -= 50;
    const QString caption = ResourceCaption(captionId);
    if (id == IDT_BENCH_SPEED)
        widget->setProperty("languageRemoveColon", true);
    if (!caption.isEmpty()) {
        QtBindResourceText(widget, captionId, caption);
    }
    return widget;
}
QLabel *CQtBenchmarkWindow::label(unsigned id, const char *name, const QString &text) {
    auto *view = new QLabel(text, this);
    Register(id, name, view);
    view->setTextInteractionFlags(Qt::TextSelectableByMouse);
    view->installEventFilter(this);
    return view;
}
QLabel *CQtBenchmarkWindow::rightLabel(unsigned id, const char *name, const QString &text) {
    auto *view = label(id, name, text);
    view->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return view;
}
QComboBox *CQtBenchmarkWindow::combo(unsigned id, const char *name) {
    auto *view = new QComboBox(this);
    Register(id, name, view);
    connect(view, &QComboBox::activated, this, [this, id](int) { OnCommand(1, id, 0); });
    return view;
}
void CQtBenchmarkWindow::BuildControls(bool totalMode) {
    auto *buttons = BuildFooter(totalMode);
    auto *outer = QtCreateDialogLayout(*this, *buttons);
    if (totalMode) {
        setObjectName("IDD_BENCH_TOTAL");
        auto *elapsed = new QHBoxLayout;
        elapsed->addWidget(
            label(IDT_BENCH_ELAPSED, "IDT_BENCH_ELAPSED", QtLangText(IDT_BENCH_ELAPSED)));
        elapsed->addWidget(rightLabel(IDT_BENCH_ELAPSED_VAL, "IDT_BENCH_ELAPSED_VAL"));
        elapsed->addStretch();
        outer->addLayout(elapsed);
        auto *console = new QPlainTextEdit(this);
        QtConfigureTextReport(*console, font());
        Register(IDE_BENCH2_EDIT, "IDE_BENCH2_EDIT", console);
        outer->addWidget(console, 1);
        auto *copy = buttons->findChild<QPushButton *>("copyReportButton");
        copy->setEnabled(false);
        connect(console, &QPlainTextEdit::textChanged, copy,
                [console, copy] { copy->setEnabled(!console->toPlainText().isEmpty()); });
    } else {
        auto *body = new QHBoxLayout;
        BuildNormalControls(body);
        outer->addLayout(body, 1);
    }
    resize(QSize(totalMode ? 760 : 960, 560).expandedTo(minimumSizeHint()));
}
QDialogButtonBox *CQtBenchmarkWindow::BuildFooter(bool totalMode) {
    auto *footer = new QtLangButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Help, this);
    footer->setObjectName("benchmarkButtons");
    for (const auto &entry :
         {qMakePair(2u, QDialogButtonBox::Close), qMakePair(9u, QDialogButtonBox::Help)}) {
        auto *view = footer->button(entry.second);
        controls_.insert(entry.first, view);
        view->setObjectName(entry.first == 2 ? "IDCANCEL" : "IDHELP");
    }
    connect(footer, &QDialogButtonBox::rejected, this, [this] { OnCancel(); });
    connect(footer, &QDialogButtonBox::helpRequested, this, [this] { OnHelp(); });
    auto *copy = footer->addButton(QString(), QDialogButtonBox::ActionRole);
    copy->setObjectName("copyReportButton");
    QtBindLocalText(copy, "DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Copy"));
    connect(copy, &QPushButton::clicked, this, &CQtBenchmarkWindow::CopyReport);
    if (!totalMode) {
        for (const auto &entry :
             {qMakePair(IDB_RESTART, "IDB_RESTART"), qMakePair(IDB_STOP, "IDB_STOP")}) {
            auto *view = new QPushButton(QtLangText(entry.first), this);
            Register(entry.first, entry.second, view);
            connect(view, &QPushButton::clicked, this,
                    [this, id = entry.first, view] { OnButtonClicked(id, view); });
            footer->addButton(view, QDialogButtonBox::ActionRole);
        }
    }
    return footer;
}
QGridLayout *CQtBenchmarkWindow::BuildParameters() {
    auto *grid = new QGridLayout;
    grid->setVerticalSpacing(8);
    grid->setHorizontalSpacing(20);
    grid->addWidget(
        label(IDT_BENCH_DICTIONARY, "IDT_BENCH_DICTIONARY", QtLangText(IDT_BENCH_DICTIONARY)), 0,
        0);
    grid->addWidget(combo(IDC_BENCH_DICTIONARY, "IDC_BENCH_DICTIONARY"), 1, 0);
    auto *memory = new QVBoxLayout;
    memory->setSpacing(0);
    memory->addWidget(label(IDT_BENCH_MEMORY, "IDT_BENCH_MEMORY", QtLangText(IDT_BENCH_MEMORY)));
    memory->addWidget(label(IDT_BENCH_MEMORY_VAL, "IDT_BENCH_MEMORY_VAL"));
    grid->addLayout(memory, 0, 2, 2, 1);
    grid->addWidget(
        label(IDT_BENCH_NUM_THREADS, "IDT_BENCH_NUM_THREADS", QtLangText(IDT_BENCH_NUM_THREADS)), 0,
        1);
    grid->addWidget(combo(IDC_BENCH_NUM_THREADS, "IDC_BENCH_NUM_THREADS"), 1, 1);
    grid->addWidget(label(IDT_BENCH_HARDWARE_THREADS, "IDT_BENCH_HARDWARE_THREADS"), 2, 1);
    qobject_cast<QLabel *>(GetItem(IDT_BENCH_DICTIONARY))->setBuddy(GetItem(IDC_BENCH_DICTIONARY));
    qobject_cast<QLabel *>(GetItem(IDT_BENCH_NUM_THREADS))
        ->setBuddy(GetItem(IDC_BENCH_NUM_THREADS));
    return grid;
}
void CQtBenchmarkWindow::BuildNormalControls(QHBoxLayout *body) {
    auto *left = new QWidget(this);
    auto *grid = new QGridLayout(left);
    grid->setContentsMargins(0, 0, style()->pixelMetric(QStyle::PM_ScrollBarExtent), 0);
    grid->addLayout(BuildParameters(), 0, 0, 1, 2);
    grid->addLayout(BuildMetrics(), 1, 0, 1, 2);
    auto *error = rightLabel(IDT_BENCH_ERROR_MESSAGE, "IDT_BENCH_ERROR_MESSAGE");
    error->setWordWrap(true);
    grid->addWidget(error, 2, 0, 1, 2, Qt::AlignLeft);
    grid->addLayout(BuildSummary(), 3, 0, 1, 2);
    const unsigned info[] = {IDT_BENCH_CPU, IDT_BENCH_CPU_FEATURE, IDT_BENCH_VER, IDT_BENCH_SYS1,
                             IDT_BENCH_SYS2};
    const char *names[] = {"IDT_BENCH_CPU", "IDT_BENCH_CPU_FEATURE", "IDT_BENCH_VER",
                           "IDT_BENCH_SYS1", "IDT_BENCH_SYS2"};
    for (int i = 0; i < 5; ++i) {
        auto *view = label(info[i], names[i]);
        if (i >= 3)
            view->hide();
        view->setWordWrap(true);
        if (i == 0 || i == 2)
            view->setAlignment(Qt::AlignRight | Qt::AlignTop);
        if (i == 0)
            grid->addWidget(view, 5, 0, 1, 2, Qt::AlignLeft);
        else if (i == 1 || i == 2) {
            view->setAlignment((i == 2 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop);
            grid->addWidget(view, 6, i - 1);
        } else
            grid->addWidget(view, i + 4, 0, 1, 2, Qt::AlignLeft);
    }
    grid->setColumnStretch(0, 232);
    grid->setColumnStretch(1, 100);
    grid->setRowStretch(9, 1);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("benchmarkControlsScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::StrongFocus);
    scroll->setWidgetResizable(true);
    scroll->setWidget(left);
    scroll->viewport()->installEventFilter(this);
    // Compact views scroll the full report while retaining the common footer.
    body->addWidget(scroll, 1);
    auto *log = new QPlainTextEdit(this);
    Register(IDT_BENCH_LOG, "IDT_BENCH_LOG", log);
    QtConfigureTextReport(*log, font());
    log->setMinimumHeight(log->fontMetrics().height() * 4);
    log->hide();
    grid->addWidget(log, 4, 0, 1, 2, Qt::AlignLeft);
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
    connect(&timer_, &QTimer::timeout, this, [this, id] { OnTimer(id, 0); });
    timer_.start(int(milliseconds));
    return id;
}
void CQtBenchmarkWindow::KillTimer(quintptr) { timer_.stop(); }
bool CQtBenchmarkWindow::PostMsg(unsigned message, quintptr wparam) {
    // Original PostMsg_Finish succeeds without posting after Attach(NULL).
    if (!postsEnabled_)
        return true;
    return QMetaObject::invokeMethod(
        this, [this, message, wparam] { OnMessage(message, wparam, 0); }, Qt::QueuedConnection);
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
void CQtBenchmarkWindow::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    OnSize(0, event->size().width(), event->size().height());
}
