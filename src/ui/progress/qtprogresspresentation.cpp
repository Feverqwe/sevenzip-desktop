// Qt presentation for the native ProgressDialog2; see ProgressDialog2.cpp provenance.
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "filemanager/views/qtlistview.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/progress/qtprogresscontrols.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

void CQtProgressDialog::BuildPresentation() {
    auto *pause = new QPushButton(QStringLiteral("&Pause"), this);
    AddControl(IDB_PAUSE, "IDB_PAUSE", pause);
    auto *stop = new QPushButton(this);
    QtBindLocalText(stop, "ProgressRedesign", QT_TRANSLATE_NOOP("ProgressRedesign", "Stop…"));
    AddControl(2, "IDCANCEL", stop);
    auto *buttons = new QDialogButtonBox(this);
    buttons->setObjectName("progressButtons");
    buttons->addButton(pause, QDialogButtonBox::ActionRole);
    buttons->addButton(stop, QDialogButtonBox::RejectRole);
    auto *outer = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    auto *status = new QtWrappingLabel(this);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    AddControl(IDT_PROGRESS_STATUS, "IDT_PROGRESS_STATUS", status);
    outer->addWidget(status);
    auto *row = new QHBoxLayout;
    auto *file = new QtWrappingLabel(this);
    file->setWordWrap(true);
    file->setTextFormat(Qt::PlainText);
    file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    AddControl(IDT_PROGRESS_FILE_NAME, "IDT_PROGRESS_FILE_NAME", file);
    file->hide();
    row->addWidget(file, 1);
    _percent = new QLabel(this);
    _percent->setObjectName("progressPercent");
    row->addWidget(_percent, 0, Qt::AlignTop);
    outer->addLayout(row);
    auto *bar = new QProgressBar(this);
    bar->setTextVisible(false);
    bar->setRange(0, 0);
    AddControl(IDC_PROGRESS1, "IDC_PROGRESS1", bar);
    outer->addWidget(bar);

    auto *places = new QWidget(this);
    auto *placeLayout = new QVBoxLayout(places);
    placeLayout->setContentsMargins(16, 0, 0, 0);
    _source = new QtWrappingLabel(places);
    _destination = new QtWrappingLabel(places);
    for (auto *label : {_source, _destination}) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        placeLayout->addWidget(label);
    }
    _source->setObjectName("progressSource");
    _destination->setObjectName("progressDestination");
    auto *locations = new QtDisclosure(places, this);
    locations->setObjectName("progressLocations");
    QtBindLocalText(locations->button(), "ProgressRedesign",
                    QT_TRANSLATE_NOOP("ProgressRedesign", "Locations"));
    _locations = locations;
    outer->addWidget(locations);
    locations->hide();
    auto *scroll = findChild<QScrollArea *>("dialogContentScroll");
    connect(locations->button(), &QToolButton::toggled, this,
            [this, locations, places, scroll](bool expanded) {
                if (expanded && scroll)
                    QTimer::singleShot(0, this, [locations, places, scroll] {
                        if (locations->isExpanded() && places->isVisible())
                            scroll->ensureWidgetVisible(places);
                    });
            });

    auto *messages = new CListView(this);
    messages->setHeaderHidden(true);
    messages->setColumnCount(2);
    messages->setTextElideMode(Qt::ElideNone);
    messages->setMinimumHeight(fontMetrics().height() * 4);
    messages->setMaximumHeight(fontMetrics().height() * 12);
    AddControl(IDL_PROGRESS_MESSAGES, "IDL_PROGRESS_MESSAGES", messages);
    outer->addWidget(messages);
    messages->hide();
    setTabOrder(pause, stop);
    resize(560, minimumSizeHint().height());
}

void CQtProgressDialog::ShowProgressPercent(const QString &percent) {
    _percent->setText(percent);
    _percent->setVisible(!percent.isEmpty());
}

void CQtProgressDialog::ShowMessages(bool visible) {
    ShowItem_Bool(IDL_PROGRESS_MESSAGES, visible);
}

void CQtProgressDialog::SetLocations(const QString &source, const QString &destination) {
    auto *locations = static_cast<QtDisclosure *>(_locations);
    QtBindLocalText(locations->button(), "ProgressRedesign",
                    destination.isEmpty()
                        ? QT_TRANSLATE_NOOP("ProgressRedesign", "Source locations")
                        : QT_TRANSLATE_NOOP("ProgressRedesign", "Destination"));
    locations->setSummary(destination);
    QtBindLocalText(_source, "ProgressRedesign",
                    QT_TRANSLATE_NOOP("ProgressRedesign", "Source: %1"), false, {source});
    _source->setAccessibleDescription(source);
    _source->setVisible(!source.isEmpty());
    QtBindLocalText(_destination, "ProgressRedesign",
                    QT_TRANSLATE_NOOP("ProgressRedesign", "Destination: %1"), false, {destination});
    _destination->setAccessibleDescription(destination);
    _destination->setVisible(!destination.isEmpty());
    _locations->setVisible(!source.isEmpty() || !destination.isEmpty());
}
