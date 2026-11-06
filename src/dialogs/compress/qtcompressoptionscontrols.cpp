#include "dialogs/compress/qtcompressoptionscontrols.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

void CQtCompressCombo::Attach(QWidget *widget) { view_ = static_cast<QComboBox *>(widget); }
int CQtCompressCombo::GetCount() const { return view_->count(); }
quint32 CQtCompressCombo::GetItemData_of_CurSel() const {
    return GetItemData(view_->currentIndex());
}
quint32 CQtCompressCombo::GetItemData(int index) const {
    // Native CB_GETITEMDATA returns CB_ERR for an absent/out-of-range item.
    if (index < 0 || index >= view_->count())
        return quint32(-1);
    return view_->itemData(index).toUInt();
}
void CQtCompressCombo::SetItemText(int index, const UString &text) {
    view_->setItemText(index, QString::fromWCharArray(text));
}
int CQtCompressCombo::AddString_SetItemData(const UString &text, qintptr value) {
    const bool hadSelection = view_->currentIndex() >= 0;
    view_->addItem(QString::fromWCharArray(text.Ptr()), quint32(value));
    // Native AddString does not select the first item. SetPrec owns selection.
    if (!hadSelection)
        view_->setCurrentIndex(-1);
    return view_->count() - 1;
}
void CQtCompressCombo::ResetContent() { view_->clear(); }
void CQtCompressCombo::SetCurSel(int index) { view_->setCurrentIndex(index); }

CQtCompressOptionsPanel::CQtCompressOptionsPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("compressOptionsContent");
    buildControls();
}

void CQtCompressOptionsPanel::buildControls() {
    auto *content = this;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
#ifdef Q_OS_MACOS
    // QMacStyle's group backing extends below its layout item. Keep that
    // pixel inside the content widget even at the initial minimum height.
    layout->setContentsMargins(0, 0, 0, 1);
#endif
    layout->setSpacing(8);
    const auto registerControl = [this](unsigned id, const char *name, QWidget *widget) {
        controls_.insert(id, widget);
        widget->setObjectName(name);
        if (id == IDX_COMPRESS_ZTIME || id == IDX_COMPRESS_PRESERVE_ATIME)
            widget->setProperty("languageWrapWidth", 300);
        if (id == IDX_COMPRESS_MTIME || id == IDX_COMPRESS_CTIME || id == IDX_COMPRESS_ATIME)
            widget->setProperty("languageWrapWidth", 300);
        if (id >= IDX_COMPRESS_NT_SYM_LINKS && id <= IDX_COMPRESS_PRESERVE_ATIME)
            QtBindLangText(widget, id);
    };
    const auto checkbox = [&](unsigned id, const char *name, const QString &text, QWidget *parent) {
        auto *box = new QCheckBox(text.isEmpty() ? QStringLiteral(":") : text, parent);
        if (text.isEmpty())
            box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        registerControl(id, name, box);
        if (text.isEmpty()) {
            box->setText(QCoreApplication::translate("qtcompressoptionscontrols", "Set"));
            QtBindLocalToolTip(box, "qtcompressoptionscontrols",
                               QT_TRANSLATE_NOOP("qtcompressoptionscontrols",
                                                 "Override the format default for this option."));
        }
        if (!text.isEmpty())
            QtBindResourceText(box, id, text);
        connect(box, &QCheckBox::clicked, this, [this, id, box] { OnButtonClicked(id, box); });
        return box;
    };
    auto *info = new QtWrappingLabel(content);
    info->setWordWrap(true);
    registerControl(IDT_COMPRESS_TIME_INFO, "IDT_COMPRESS_TIME_INFO", info);
    layout->addWidget(info);
    layout->addSpacing(12);
    auto *ntfsSlot = new QWidget(this);
    ntfsSlot->setObjectName("compressOptionsNtfsSlot");
    auto *slot = new QVBoxLayout(ntfsSlot);
    slot->setContentsMargins(0, 0, 0, 0);
#ifdef Q_OS_MACOS
    // QMacStyle's group backing rectangle extends one logical pixel below
    // its layout item; retain that pixel inside the slot's exposed area.
    slot->setContentsMargins(0, 0, 0, 1);
#endif
    slot->setSpacing(0);
    auto *ntfs = new QGroupBox("NTFS", ntfsSlot);
    ntfs->setFlat(true);
    ntfs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    registerControl(IDG_COMPRESS_NTFS, "IDG_COMPRESS_NTFS", ntfs);
#ifdef Q_OS_WIN
    QtBindLocalText(ntfs, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Links and metadata"));
#else
    QtBindLocalText(ntfs, "ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Links"));
#endif
    auto *ntfsLayout = new QGridLayout(ntfs);
    ntfsLayout->setVerticalSpacing(8);
    ntfsLayout->addWidget(checkbox(IDX_COMPRESS_NT_SYM_LINKS, "IDX_COMPRESS_NT_SYM_LINKS",
                                   "Store symbolic links", ntfs),
                          0, 0);
    ntfsLayout->addWidget(checkbox(IDX_COMPRESS_NT_HARD_LINKS, "IDX_COMPRESS_NT_HARD_LINKS",
                                   "Store hard links", ntfs),
                          1, 0);
    ntfsLayout->addWidget(checkbox(IDX_COMPRESS_NT_ALT_STREAMS, "IDX_COMPRESS_NT_ALT_STREAMS",
                                   "Store alternate data streams", ntfs),
                          2, 0);
    ntfsLayout->addWidget(
        checkbox(IDX_COMPRESS_NT_SECUR, "IDX_COMPRESS_NT_SECUR", "Store file security", ntfs), 3,
        0);
    slot->addWidget(ntfs);
    layout->addWidget(ntfsSlot);
    auto *time = new QGroupBox("Time", this);
    time->setFlat(true);
    time->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    registerControl(IDG_COMPRESS_TIME, "IDG_COMPRESS_TIME", time);
    QtBindResourceText(time, IDG_COMPRESS_TIME, "Time");
    auto *grid = new QGridLayout(time);
    grid->setVerticalSpacing(8);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->addWidget(checkbox(IDX_COMPRESS_PREC_SET, "IDX_COMPRESS_PREC_SET", {}, time), 0, 0, 1, 3);
    QtBindLocalText(GetItem(IDX_COMPRESS_PREC_SET), "qtcompressoptionscontrols",
                    QT_TRANSLATE_NOOP("qtcompressoptionscontrols", "Set time precision"), true);
    static_cast<QCheckBox *>(GetItem(IDX_COMPRESS_PREC_SET))
        ->setText(QCoreApplication::translate("qtcompressoptionscontrols", "Set time precision"));
    auto *precisionLabel = new QtWrappingLabel("Timestamp precision:", time);
    precisionLabel->setWordWrap(true);
    registerControl(IDT_COMPRESS_TIME_PREC, "IDT_COMPRESS_TIME_PREC", precisionLabel);
    QtBindResourceText(precisionLabel, IDT_COMPRESS_TIME_PREC, "Timestamp precision:");
    precisionLabel->setContentsMargins(24, 0, 0, 0);
    grid->addWidget(precisionLabel, 1, 0, 1, 2);
    auto *precision = new QComboBox(time);
    precision->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    precision->setMinimumContentsLength(12);
    precision->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    registerControl(IDC_COMPRESS_TIME_PREC, "IDC_COMPRESS_TIME_PREC", precision);
    precisionLabel->setBuddy(precision);
    grid->addWidget(precision, 1, 2);
    connect(precision, &QComboBox::activated, this,
            [this](int) { OnCommand(CBN_SELCHANGE, IDC_COMPRESS_TIME_PREC, 0); });
    struct CTimeRow {
        unsigned set, value;
        const char *setName, *valueName, *text;
    };
    const CTimeRow rows[] = {{IDX_COMPRESS_MTIME_SET, IDX_COMPRESS_MTIME, "IDX_COMPRESS_MTIME_SET",
                              "IDX_COMPRESS_MTIME", "Store modification time"},
                             {IDX_COMPRESS_CTIME_SET, IDX_COMPRESS_CTIME, "IDX_COMPRESS_CTIME_SET",
                              "IDX_COMPRESS_CTIME", "Store creation time"},
                             {IDX_COMPRESS_ATIME_SET, IDX_COMPRESS_ATIME, "IDX_COMPRESS_ATIME_SET",
                              "IDX_COMPRESS_ATIME", "Store last access time"},
                             {IDX_COMPRESS_ZTIME_SET, IDX_COMPRESS_ZTIME, "IDX_COMPRESS_ZTIME_SET",
                              "IDX_COMPRESS_ZTIME", "Set archive time to latest file time"}};
    int row = 2;
    for (const auto &item : rows) {
        auto *set = checkbox(item.set, item.setName, {}, time);
        QtBindLangText(set, item.value, {}, true);
        grid->addWidget(set, row, 0);
        grid->addWidget(checkbox(item.value, item.valueName, item.text, time), row, 1, 1, 2);
        ++row;
    }
    auto *preserve = checkbox(IDX_COMPRESS_PRESERVE_ATIME, "IDX_COMPRESS_PRESERVE_ATIME",
                              "Do not change source files last access time", time);
#ifndef Q_OS_WIN
    preserve->setEnabled(false);
    preserve->hide();
    QtBindLocalToolTip(
        preserve, "qtcompressoptionscontrols",
        QT_TRANSLATE_NOOP("qtcompressoptionscontrols",
                          "The official engine supports this option only on Windows."));
#endif
    grid->addWidget(preserve, row, 0, 1, 3);
    layout->addWidget(time);
    layout->addStretch();
}

void CQtCompressOptionsPanel::initialize() {
    if (initialized_)
        return;
    initialized_ = true;
    OnInit();
}

void CQtCompressOptionsPanel::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && initialized_) {
        OnLanguageChange();
        // Resource bindings retain the descriptive accessible option names.
        // The visible override caption is shared by these source controls.
        for (unsigned id : {IDX_COMPRESS_PREC_SET, IDX_COMPRESS_MTIME_SET, IDX_COMPRESS_CTIME_SET,
                            IDX_COMPRESS_ATIME_SET, IDX_COMPRESS_ZTIME_SET})
            static_cast<QCheckBox *>(GetItem(id))
                ->setText(QCoreApplication::translate(
                    "qtcompressoptionscontrols",
                    id == IDX_COMPRESS_PREC_SET ? "Set time precision" : "Set"));
    }
    QWidget::changeEvent(event);
}
QWidget *CQtCompressOptionsPanel::GetItem(unsigned id) const { return controls_.value(id); }
void CQtCompressOptionsPanel::CheckButton(unsigned id, bool value) {
    static_cast<QCheckBox *>(GetItem(id))->setChecked(value);
}
bool CQtCompressOptionsPanel::IsButtonCheckedBool(unsigned id) const {
    return static_cast<QCheckBox *>(GetItem(id))->isChecked();
}
void CQtCompressOptionsPanel::ShowItem_Bool(unsigned id, bool visible) {
    GetItem(id)->setVisible(visible);
    if (id == IDG_COMPRESS_NTFS) {
        GetItem(id)->parentWidget()->setVisible(visible);
    }
}
void CQtCompressOptionsPanel::EnableItem(unsigned id, bool enabled) {
    GetItem(id)->setEnabled(enabled);
}
void CQtCompressOptionsPanel::SetItemText(unsigned id, const UString &text) {
    QString value = QString::fromWCharArray(text.Ptr());
    if (id == IDT_COMPRESS_TIME_INFO && value.contains(':'))
        value = QtLangText(IDT_COMPRESS_FORMAT, "Archive format:").remove('&') +
                value.mid(value.indexOf(':') + 1);
    static_cast<QLabel *>(GetItem(id))->setText(value);
}
bool CQtCompressOptionsPanel::OnButtonClicked(unsigned, QWidget *) { return false; }
