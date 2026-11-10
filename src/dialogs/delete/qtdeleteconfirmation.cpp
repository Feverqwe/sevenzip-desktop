#include "dialogs/delete/qtdeleteconfirmation.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtcommandicons.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QShowEvent>
#include <QStringListModel>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>
#include <utility>

namespace {
class DeleteNamesModel final : public QStringListModel {
  public:
    using QStringListModel::QStringListModel;

    QVariant data(const QModelIndex &index, int role) const override {
        if (role == Qt::ToolTipRole && index.isValid())
            return Qt::convertFromPlainText(
                QStringListModel::data(index, Qt::DisplayRole).toString(), Qt::WhiteSpacePre);
        return QStringListModel::data(index, role);
    }
};

class DeleteNamesView final : public QListView {
  public:
    DeleteNamesView(const QStringList &names, QWidget *parent) : QListView(parent) {
        setObjectName("deleteSelectedNames");
        setModel(new DeleteNamesModel(names, this));
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setSelectionMode(QAbstractItemView::NoSelection);
        setUniformItemSizes(true);
        setTextElideMode(Qt::ElideMiddle);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        updateHeight();
    }

  protected:
    void changeEvent(QEvent *event) override {
        QListView::changeEvent(event);
        if (model() &&
            (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange))
            updateHeight();
    }

  private:
    void updateHeight() {
        setFixedHeight(sizeHintForRow(0) * qMin(8, model()->rowCount()) + 2 * frameWidth());
    }
};
} // namespace

QtDeleteConfirmation::QtDeleteConfirmation(Request request, QWidget *parent)
    : QDialog(parent), request_(std::move(request)) {
    setObjectName("deleteConfirmation");
    QtUseContextDialogModality(*this);
    buildPresentation();
    refreshText();
    ensurePolished();
    resize(560, minimumSizeHint().height());
}

void QtDeleteConfirmation::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!initialSize_)
        return;
    initialSize_ = false;
    QtFitScrollableDialog(*this, 420);
}

void QtDeleteConfirmation::buildPresentation() {
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->setObjectName("deleteButtons");
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setObjectName("deleteCancel");
    QtBindStandardResourceText(cancel, 2, "Cancel");
    confirm_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    confirm_->setObjectName("deleteConfirm");
    confirm_->setEnabled(!request_.readOnly && !request_.names.isEmpty());
    connect(confirm_, &QPushButton::clicked, this, &QtDeleteConfirmation::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    cancel->setDefault(true);
    cancel->setFocus();
    auto *body = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    const auto label = [&](const char *name) {
        auto *widget = new QtWrappingLabel(this);
        widget->setObjectName(name);
        widget->setTextFormat(Qt::PlainText);
        widget->setWordWrap(true);
        widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        body->addWidget(widget);
        return widget;
    };
    heading_ = label("deleteHeading");
    QFont font = heading_->font();
    font.setWeight(QFont::DemiBold);
    heading_->setFont(font);
    const QIcon icon = !request_.icon.isNull() ? request_.icon
                       : request_.names.size() == 1 && !request_.includesFolders
                           ? style()->standardIcon(QStyle::SP_FileIcon)
                           : QtCommandSymbol(QtCommandIcon::Folder);
    source_ = new QtSourceSummary(icon, {}, request_.location, this);
    source_->setObjectName("deleteSourceSummary");
    body->addWidget(source_);
    folders_ = label("deleteFolders");
    folders_->setVisible(request_.includesFolders);
    consequence_ = label("deleteConsequence");
    restriction_ = label("deleteRestriction");
    restriction_->setVisible(request_.readOnly);
    if (request_.names.size() > 1)
        body->addWidget(new DeleteNamesView(request_.names, this));
}

void QtDeleteConfirmation::refreshText() {
    const auto text = [](const char *value) {
        return QCoreApplication::translate("DeleteRedesign", value);
    };
    const char *title = QT_TRANSLATE_NOOP("DeleteRedesign", "Move to Trash?");
    const char *action = QT_TRANSLATE_NOOP("DeleteRedesign", "Move to Trash");
    const char *consequence = QT_TRANSLATE_NOOP(
        "DeleteRedesign",
        "You can restore the items from Trash after they have been moved successfully.");
    if (request_.mode == Mode::Archive) {
        title = QT_TRANSLATE_NOOP("DeleteRedesign", "Delete from archive?");
        action = QT_TRANSLATE_NOOP("DeleteRedesign", "Delete from archive");
        consequence = QT_TRANSLATE_NOOP(
            "DeleteRedesign", "This changes the archive. Deleted entries do not go to Trash.");
    }
    setWindowTitle(text(title));
    heading_->setText(text(title));
    confirm_->setText(text(action));
    consequence_->setText(text(consequence));
    folders_->setText(text(
        QT_TRANSLATE_NOOP("DeleteRedesign", "The contents of selected folders are included.")));
    restriction_->setText(text(
        QT_TRANSLATE_NOOP("DeleteRedesign", "This source is read-only. Deletion is unavailable.")));
    const QString first = request_.names.value(0);
    const QString baseName = QFileInfo(first).fileName();
    const QString name = request_.names.size() == 1
                             ? (baseName.isEmpty() ? first : baseName)
                             : text(QT_TRANSLATE_NOOP("DeleteRedesign", "Selected items: %1"))
                                   .arg(request_.names.size());
    const QString path = request_.names.size() == 1
                             ? QDir(request_.location).filePath(request_.names.first())
                             : request_.location;
    source_->SetSource(name, path);
}

void QtDeleteConfirmation::accept() {
    if (!request_.readOnly && !request_.names.isEmpty())
        QDialog::accept();
}

void QtDeleteConfirmation::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && source_)
        refreshText();
    QDialog::changeEvent(event);
}
