#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "benchmark/qtbenchmarkcontrols.h"
#include "localization/qtlangcontrols.h"
#include "ui/reports/qttextreport.h"
#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QLabel>
#include <QSet>

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
    const unsigned captions[] = {IDT_BENCH_SIZE, IDT_BENCH_SPEED, IDT_BENCH_USAGE_LABEL,
                                 IDT_BENCH_RPU_LABEL, IDT_BENCH_RATING_LABEL};
    const char *names[] = {"IDT_BENCH_SIZE", "IDT_BENCH_SPEED", "IDT_BENCH_USAGE_LABEL",
                           "IDT_BENCH_RPU_LABEL", "IDT_BENCH_RATING_LABEL"};
    for (int column = 0; column < 5; ++column) {
        auto *header = rightLabel(captions[column], names[column], QtLangText(captions[column]));
        header->setWordWrap(true);
        grid->addWidget(header, 0, column + 2);
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

void CQtBenchmarkWindow::CopyReport() {
    if (auto *console = qobject_cast<QPlainTextEdit *>(GetItem(IDE_BENCH2_EDIT))) {
        if (!console->toPlainText().isEmpty())
            QtCopyTextReport(*console);
        return;
    }
    QStringList lines;
    for (unsigned id : {IDC_BENCH_DICTIONARY, IDC_BENCH_NUM_THREADS}) {
        auto *value = qobject_cast<QComboBox *>(GetItem(id));
        const unsigned caption =
            id == IDC_BENCH_DICTIONARY ? IDT_BENCH_DICTIONARY : IDT_BENCH_NUM_THREADS;
        lines << QtLangText(caption).remove('&') + '\t' + value->currentText();
    }
    for (int row = 0; row < metrics_->rowCount(); ++row) {
        QStringList cells;
        QSet<QWidget *> seen;
        bool hasValues = false;
        for (int column = 0; column < metrics_->columnCount(); ++column) {
            auto *item = metrics_->itemAtPosition(row, column);
            auto *view = item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
            if (view && !seen.contains(view)) {
                cells << view->text();
                seen.insert(view);
                hasValues = true;
            } else
                cells << QString();
        }
        if (hasValues)
            lines << cells.join('\t');
    }
    for (const auto &pair : {qMakePair(IDT_BENCH_MEMORY, IDT_BENCH_MEMORY_VAL),
                             qMakePair(IDT_BENCH_ELAPSED, IDT_BENCH_ELAPSED_VAL),
                             qMakePair(IDT_BENCH_PASSES, IDT_BENCH_PASSES_VAL)}) {
        auto *value = qobject_cast<QLabel *>(GetItem(pair.second));
        QString text = value->text();
        if (pair.first == IDT_BENCH_PASSES)
            text += ' ' + qobject_cast<QComboBox *>(GetItem(IDC_BENCH_NUM_PASSES))->currentText();
        lines << QtLangText(pair.first).remove('&') + '\t' + text;
    }
    for (unsigned id : {IDT_BENCH_HARDWARE_THREADS, IDT_BENCH_CPU, IDT_BENCH_CPU_FEATURE,
                        IDT_BENCH_VER, IDT_BENCH_SYS1, IDT_BENCH_SYS2, IDT_BENCH_ERROR_MESSAGE}) {
        auto *value = qobject_cast<QLabel *>(GetItem(id));
        if (value && !value->text().isEmpty())
            lines << value->text();
    }
    if (auto *log = qobject_cast<QPlainTextEdit *>(GetItem(IDT_BENCH_LOG));
        log && !log->toPlainText().isEmpty())
        lines << log->toPlainText();
    QApplication::clipboard()->setText(lines.join('\n'));
}
