#include "dialogs/compress/createarchivedialog.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>

CreateArchiveDialog::CreateArchiveDialog(const QString &directory,
                                         const QStringList &initialSources, QWidget *parent,
                                         std::shared_ptr<const ArchiveCodecCatalog> codecs,
                                         const CCompressDialog *sourceDialog)
    : QDialog(parent), codecs_(std::move(codecs)) {
    QtUseContextDialogModality(*this);
    setObjectName("createArchiveDialog");
    auto *layout = new QVBoxLayout(this);
    const int horizontalMargin = 16;
    const int verticalMargin = 16;
    // Put the scroll bar at the window edge; the source right margin belongs
    // between the form controls and that bar, not outside the scroll area.
    layout->setContentsMargins(horizontalMargin, verticalMargin, 0, verticalMargin);
    layout->setSpacing(16);
    auto *scroll = createOptionsScrollArea(this);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, horizontalMargin, 0);
    contentLayout->setSpacing(16);
    contentLayout->setAlignment(Qt::AlignTop);
    m_RegistryInfo.Load(compressionSettingsPath());
    fixedSources_ = sourceDialog != nullptr;
    if (sourceDialog)
        archiveNameState_ = *sourceDialog;
    else
        archiveNameState_.DirPrefix = directory;
    archiveNameState_.StartDirPrefix = directory;
    prepareInitialSources(initialSources);
    createArchiveForm(contentLayout, layout);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    createFooter(layout);
    buttons_->setContentsMargins(0, 0, horizontalMargin, 0);
    // Cocoa layout-item margins can extend a combo or group frame past its
    // allocated column/scroll area. Keep the complete source controls exposed.
    for (auto *combo : findChildren<QComboBox *>())
        combo->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    for (auto *combo : {format_, level_, method_, dictionary_, wordSize_, solidBlock_, threads_,
                        memUse_, updateMode_, pathMode_, encryption_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(combo == memUse_ ? 6 : 10);
    }
    for (auto *group : findChildren<QGroupBox *>())
        group->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#ifdef Q_OS_MACOS
    // Cocoa checkbox layout-item margins can extend a spanning form row
    // beyond its group and scroll viewport.
    for (auto *check : findChildren<QCheckBox *>())
        check->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
    // Source MY_CHECKBOX/PushButton controls participate in Tab traversal.
    // QMacStyle otherwise gives buttons only the restricted TabFocus policy.
    for (auto *button : findChildren<QAbstractButton *>())
        button->setFocusPolicy(Qt::StrongFocus);
    BindLanguage();
    if (fixedSources_)
        initializeSourceControls(sourceDialog->Info);
    updateRedesignSummary();
    stabilizeColumnWidths();
    // Size the source form to its controls rather than reserving the height
    // of the former file picker. Leave room for the native window frame and
    // taskbar; the options can scroll on a smaller screen, the footer cannot.
    const QMargins margins = layout->contentsMargins();
    const int width =
        qMax(640, content->minimumSizeHint().width() + margins.left() + margins.right());
    const int contentWidth =
        width - margins.left() - margins.right() - scroll->verticalScrollBar()->sizeHint().width();
    // Reparenting fields and revealing the summary can leave cached spacing
    // from construction. Recompute it before choosing the initial height.
    contentLayout->invalidate();
    const int contentHeight = contentLayout->hasHeightForWidth()
                                  ? contentLayout->heightForWidth(contentWidth)
                                  : content->sizeHint().height();
    const int height =
        contentHeight + findChild<QWidget *>("archiveNameField")->sizeHint().height() +
        buttons_->sizeHint().height() + 2 * layout->spacing() + margins.top() + margins.bottom();
    const int frameHeight = 2 * style()->pixelMetric(QStyle::PM_TitleBarHeight);
    const QSize available = screen()->availableGeometry().size() - QSize(0, frameHeight);
    resize(QSize(width, height).boundedTo(available).expandedTo(minimumSizeHint()));
    // The original modal's first editable combo receives initial dialog focus
    // and selects its text. Qt's scroll hierarchy needs an explicit adapter.
    name_->setFocus(Qt::OtherFocusReason);
    name_->selectAll();
}
