#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "benchmark/qtbenchmarkcontrols.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/reports/qttextreport.h"
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <utility>

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
    connect(view, &QComboBox::activated, this, [this, id](int) { OnCommand(1, id); });
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
    auto *footer = new QtLangButtonBox(QDialogButtonBox::Close, this);
    footer->setObjectName("benchmarkButtons");
    auto *close = footer->button(QDialogButtonBox::Close);
    controls_.insert(2u, close);
    close->setObjectName("IDCANCEL");
    connect(footer, &QDialogButtonBox::rejected, this, [this] { OnCancel(); });
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
                    [this, id = entry.first] { OnButtonClicked(id); });
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
    auto *cpu = rightLabel(IDT_BENCH_CPU, "IDT_BENCH_CPU");
    cpu->setAlignment(Qt::AlignRight | Qt::AlignTop);
    auto *features = label(IDT_BENCH_CPU_FEATURE, "IDT_BENCH_CPU_FEATURE");
    features->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *version = rightLabel(IDT_BENCH_VER, "IDT_BENCH_VER");
    version->setAlignment(Qt::AlignRight | Qt::AlignTop);
    auto *system1 = label(IDT_BENCH_SYS1, "IDT_BENCH_SYS1");
    auto *system2 = label(IDT_BENCH_SYS2, "IDT_BENCH_SYS2");
    for (auto *view : {cpu, features, version, system1, system2})
        view->setWordWrap(true);
    system1->hide();
    system2->hide();
    grid->addWidget(cpu, 5, 0, 1, 2, Qt::AlignLeft);
    grid->addWidget(features, 6, 0);
    grid->addWidget(version, 6, 1);
    grid->addWidget(system1, 7, 0, 1, 2, Qt::AlignLeft);
    grid->addWidget(system2, 8, 0, 1, 2, Qt::AlignLeft);
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
QGridLayout *CQtBenchmarkWindow::BuildMetrics() {
    auto *grid = new QGridLayout;
    metrics_ = grid;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    for (int column = 0; column < 2; ++column) {
        auto *header = new QLabel(this);
        QtBindLocalText(header, "BenchmarkReport",
                        column ? QT_TRANSLATE_NOOP("BenchmarkReport", "Result")
                               : QT_TRANSLATE_NOOP("BenchmarkReport", "Operation"));
        grid->addWidget(header, 0, column);
    }
    const std::pair<unsigned, const char *> headers[] = {
        {IDT_BENCH_SIZE, "IDT_BENCH_SIZE"},
        {IDT_BENCH_SPEED, "IDT_BENCH_SPEED"},
        {IDT_BENCH_USAGE_LABEL, "IDT_BENCH_USAGE_LABEL"},
        {IDT_BENCH_RPU_LABEL, "IDT_BENCH_RPU_LABEL"},
        {IDT_BENCH_RATING_LABEL, "IDT_BENCH_RATING_LABEL"}};
    int column = 2;
    for (const auto &[id, name] : headers) {
        auto *header = rightLabel(id, name, QtLangText(id));
        header->setWordWrap(true);
        grid->addWidget(header, 0, column++);
    }
    const unsigned values[2][2][5] = {
        {{IDT_BENCH_COMPRESS_SIZE1, IDT_BENCH_COMPRESS_SPEED1, IDT_BENCH_COMPRESS_USAGE1,
          IDT_BENCH_COMPRESS_RPU1, IDT_BENCH_COMPRESS_RATING1},
         {IDT_BENCH_COMPRESS_SIZE2, IDT_BENCH_COMPRESS_SPEED2, IDT_BENCH_COMPRESS_USAGE2,
          IDT_BENCH_COMPRESS_RPU2, IDT_BENCH_COMPRESS_RATING2}},
        {{IDT_BENCH_DECOMPR_SIZE1, IDT_BENCH_DECOMPR_SPEED1, IDT_BENCH_DECOMPR_USAGE1,
          IDT_BENCH_DECOMPR_RPU1, IDT_BENCH_DECOMPR_RATING1},
         {IDT_BENCH_DECOMPR_SIZE2, IDT_BENCH_DECOMPR_SPEED2, IDT_BENCH_DECOMPR_USAGE2,
          IDT_BENCH_DECOMPR_RPU2, IDT_BENCH_DECOMPR_RATING2}}};
    for (int group = 0; group < 2; ++group) {
        const unsigned id = group ? IDG_BENCH_DECOMPRESSING : IDG_BENCH_COMPRESSING;
        grid->addWidget(
            label(id, group ? "IDG_BENCH_DECOMPRESSING" : "IDG_BENCH_COMPRESSING", QtLangText(id)),
            group * 2 + 1, 0, 2, 1);
        for (int row = 0; row < 2; ++row) {
            const unsigned textId = row ? IDT_BENCH_RESULTING : IDT_BENCH_CURRENT;
            grid->addWidget(label(group ? textId + 50 : textId,
                                  group ? (row ? "IDT_BENCH_RESULTING2" : "IDT_BENCH_CURRENT2")
                                        : (row ? "IDT_BENCH_RESULTING" : "IDT_BENCH_CURRENT"),
                                  QtLangText(textId)),
                            group * 2 + row + 1, 1);
            for (int column = 0; column < 5; ++column) {
                const unsigned id = values[group][row][column];
                const QByteArray name = "benchmarkValue" + QByteArray::number(id);
                grid->addWidget(rightLabel(id, name.constData()), group * 2 + row + 1, column + 2);
            }
        }
    }
    grid->setRowMinimumHeight(5, 8);
    grid->addWidget(
        label(IDG_BENCH_TOTAL_RATING, "IDG_BENCH_TOTAL_RATING", QtLangText(IDG_BENCH_TOTAL_RATING)),
        6, 0, 1, 4);
    grid->addWidget(rightLabel(IDT_BENCH_TOTAL_USAGE_VAL, "IDT_BENCH_TOTAL_USAGE_VAL"), 6, 4);
    grid->addWidget(rightLabel(IDT_BENCH_TOTAL_RPU_VAL, "IDT_BENCH_TOTAL_RPU_VAL"), 6, 5);
    grid->addWidget(rightLabel(IDT_BENCH_TOTAL_RATING_VAL, "IDT_BENCH_TOTAL_RATING_VAL"), 6, 6);
    return grid;
}

QGridLayout *CQtBenchmarkWindow::BuildSummary() {
    auto *summary = new QGridLayout;
    summary->setHorizontalSpacing(12);
    summary->addWidget(label(IDT_BENCH_ELAPSED, "IDT_BENCH_ELAPSED", QtLangText(IDT_BENCH_ELAPSED)),
                       0, 0);
    summary->addWidget(rightLabel(IDT_BENCH_ELAPSED_VAL, "IDT_BENCH_ELAPSED_VAL"), 0, 1);
    auto *passes = label(IDT_BENCH_PASSES, "IDT_BENCH_PASSES", QtLangText(IDT_BENCH_PASSES));
    summary->addWidget(passes, 0, 2);
    summary->addWidget(rightLabel(IDT_BENCH_PASSES_VAL, "IDT_BENCH_PASSES_VAL"), 0, 3);
    auto *limit = combo(IDC_BENCH_NUM_PASSES, "IDC_BENCH_NUM_PASSES");
    passes->setBuddy(limit);
    summary->addWidget(limit, 0, 4);
    summary->setColumnStretch(5, 1);
    return summary;
}
