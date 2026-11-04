#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "benchmark/qtbenchmarkcontrols.h"
#include "localization/qtlangcontrols.h"
#include "ui/reports/qttextreport.h"
#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QLabel>
#include <QSet>

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
