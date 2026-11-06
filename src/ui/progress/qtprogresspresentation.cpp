// Qt presentation for the native ProgressDialog2; see ProgressDialog2.cpp provenance.
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "filemanager/views/qtlistview.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/progress/qtprogresscontrols.h"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <span>

namespace {
// The standard resource IDCANCEL is 2; the port does not include Win32 controls.
constexpr unsigned kCancelID = 2;
struct ProgressRow {
    unsigned Label;
    unsigned Value;
    unsigned ExtraValue = 0;
};
constexpr ProgressRow kMainRows[] = {
    {IDT_PROGRESS_ELAPSED, IDT_PROGRESS_ELAPSED_VAL},
    {IDT_PROGRESS_REMAINING, IDT_PROGRESS_REMAINING_VAL},
    {IDT_PROGRESS_FILES, IDT_PROGRESS_FILES_VAL, IDT_PROGRESS_FILES_TOTAL},
    {IDT_PROGRESS_ERRORS, IDT_PROGRESS_ERRORS_VAL}};
constexpr ProgressRow kDetailRows[] = {{IDT_PROGRESS_TOTAL, IDT_PROGRESS_TOTAL_VAL},
                                       {IDT_PROGRESS_SPEED, IDT_PROGRESS_SPEED_VAL},
                                       {IDT_PROGRESS_PROCESSED, IDT_PROGRESS_PROCESSED_VAL},
                                       {IDT_PROGRESS_PACKED, IDT_PROGRESS_PACKED_VAL},
                                       {IDT_PROGRESS_RATIO, IDT_PROGRESS_RATIO_VAL}};
} // namespace

void CQtProgressDialog::BuildPresentation() {
    auto addLabel = [this](unsigned id, const char *name, const QString &text, bool right) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setMinimumHeight(label->fontMetrics().height());
        label->setAlignment((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop);
        AddControl(id, name, label);
    };
#define LABEL(id, text) addLabel(id, #id, QStringLiteral(text), false)
    LABEL(IDT_PROGRESS_ELAPSED, "Elapsed time:");
    LABEL(IDT_PROGRESS_REMAINING, "Remaining time:");
    LABEL(IDT_PROGRESS_FILES, "Files:");
    LABEL(IDT_PROGRESS_ERRORS, "Errors:");
    LABEL(IDT_PROGRESS_TOTAL, "Total size:");
    LABEL(IDT_PROGRESS_SPEED, "Speed:");
    LABEL(IDT_PROGRESS_PROCESSED, "Processed:");
    LABEL(IDT_PROGRESS_PACKED, "Compressed size:");
    LABEL(IDT_PROGRESS_RATIO, "Compression ratio:");
#undef LABEL
#define VALUE(id) addLabel(id, #id, QString(), true)
    VALUE(IDT_PROGRESS_ELAPSED_VAL);
    VALUE(IDT_PROGRESS_REMAINING_VAL);
    VALUE(IDT_PROGRESS_FILES_VAL);
    VALUE(IDT_PROGRESS_FILES_TOTAL);
    VALUE(IDT_PROGRESS_ERRORS_VAL);
    VALUE(IDT_PROGRESS_TOTAL_VAL);
    VALUE(IDT_PROGRESS_SPEED_VAL);
    VALUE(IDT_PROGRESS_PROCESSED_VAL);
    VALUE(IDT_PROGRESS_PACKED_VAL);
    VALUE(IDT_PROGRESS_RATIO_VAL);
#undef VALUE
    addLabel(IDT_PROGRESS_STATUS, "IDT_PROGRESS_STATUS", {}, false);
    addLabel(IDT_PROGRESS_FILE_NAME, "IDT_PROGRESS_FILE_NAME", {}, false);
    auto *bar = new QProgressBar(this);
    bar->setTextVisible(false);
    bar->setRange(0, 100);
    bar->setValue(0);
    AddControl(IDC_PROGRESS1, "IDC_PROGRESS1", bar);
    auto *messages = new CListView(this);
    messages->setHeaderHidden(true);
    messages->setColumnCount(2);
    messages->setTextElideMode(Qt::ElideNone);
    AddControl(IDL_PROGRESS_MESSAGES, "IDL_PROGRESS_MESSAGES", messages);
    auto *pause = new QPushButton(QStringLiteral("&Pause"), this);
    pause->setAutoDefault(true);
    AddControl(IDB_PAUSE, "IDB_PAUSE", pause);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setAutoDefault(true);
    cancel->setDefault(true);
    AddControl(kCancelID, "IDCANCEL", cancel);
    auto *buttons = new QDialogButtonBox(this);
    buttons->setObjectName("progressButtons");
    buttons->addButton(pause, QDialogButtonBox::ActionRole);
    buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    auto *outer = QtCreateDialogLayout(*this, *buttons);
    auto *body = new QWidget(this);
    auto *content = new QVBoxLayout(body);
    // Match the other scrolling forms: reserve a gutter for native overlays.
    content->setContentsMargins(0, 0, style()->pixelMetric(QStyle::PM_ScrollBarExtent), 0);
    content->setSpacing(8);
    content->addWidget(GetItem(IDT_PROGRESS_STATUS));
    content->addWidget(bar);
    auto *file = static_cast<QLabel *>(GetItem(IDT_PROGRESS_FILE_NAME));
    file->setMinimumHeight(fontMetrics().height() * 2);
    file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    content->addWidget(file);
    auto *mainStats = new QGridLayout;
    mainStats->setHorizontalSpacing(12);
    mainStats->setVerticalSpacing(8);
    mainStats->setColumnStretch(1, 1);
    auto addRows = [this](QGridLayout &grid, std::span<const ProgressRow> rows) {
        int row = 0;
        for (const auto &item : rows) {
            grid.addWidget(GetItem(item.Label), row, 0);
            if (item.ExtraValue) {
                auto *values = new QHBoxLayout;
                values->setSpacing(0);
                values->addStretch();
                values->addWidget(GetItem(item.Value));
                values->addWidget(GetItem(item.ExtraValue));
                grid.addLayout(values, row, 1);
            } else
                grid.addWidget(GetItem(item.Value), row, 1);
            ++row;
        }
    };
    addRows(*mainStats, kMainRows);
    content->addLayout(mainStats);
    auto *detailsBody = new QWidget(body);
    auto *stats = new QGridLayout(detailsBody);
    stats->setContentsMargins(24, 0, 0, 0);
    stats->setHorizontalSpacing(12);
    stats->setVerticalSpacing(8);
    stats->setColumnStretch(1, 1);
    addRows(*stats, kDetailRows);
    auto *details = new QtDisclosure(detailsBody, body);
    _byteStatistics = details;
    details->setObjectName("progressDetailsSection");
    details->button()->setObjectName("progressDetailsToggle");
    QtBindLocalText(details->button(), "DialogRedesign",
                    QT_TRANSLATE_NOOP("DialogRedesign", "Details"));
    content->addWidget(details);
    content->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("progressControlsScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    // Keep the overview reachable when the independent error list appears.
    // Expanding Details scrolls within this area instead of moving the footer.
    scroll->setMinimumHeight(body->minimumSizeHint().height());
    outer->addWidget(scroll, 1);
    messages->setMinimumHeight(fontMetrics().height() * 4);
    outer->addWidget(messages, 1);
    GetItem(IDT_PROGRESS_STATUS)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setTabOrder(details->button(), messages);
    setTabOrder(messages, pause);
    setTabOrder(pause, cancel);
    GetItem(IDT_PROGRESS_ERRORS)->hide();
    GetItem(IDT_PROGRESS_ERRORS_VAL)->hide();
    messages->hide();
    resize(QSize(560, minimumSizeHint().height()).expandedTo(minimumSizeHint()));
}
