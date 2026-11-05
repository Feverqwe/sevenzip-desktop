#include "dialogs/recentplaces/qtrecentplacesdialog.h"
#include "Common/StringConvert.h"
#include "filemanager/providers/RootFolder.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/theme/qtcommandicons.h"
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString Name(const QtRecentPlace &place) {
    int iconIndex = 0;
    const auto text = [](const char *source) {
        return QCoreApplication::translate("QtWindowWorkspace", source);
    };
    if (place.nativeIdentity == "computer")
        return ToQString(RootFolder_GetName_Computer(iconIndex));
    if (place.nativeIdentity == "network")
        return ToQString(RootFolder_GetName_Network(iconIndex));
    if (place.path.isEmpty() || place.nativeIdentity == "locations")
        return text("Locations");
    const QString name = QFileInfo(QDir::cleanPath(place.path)).fileName();
    return name.isEmpty() ? place.path : name;
}
} // namespace

QtRecentPlacesDialog::QtRecentPlacesDialog(QtRecentPlacesStore &store, QWidget *parent)
    : QDialog(parent), store_(store), search_(new QLineEdit(this)), list_(new QListWidget(this)),
      empty_(new QtWrappingLabel(this)), error_(new QtWrappingLabel(this)),
      remove_(new QPushButton(this)), clear_(new QPushButton(this)) {
    setObjectName("foldersHistoryDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowModality(Qt::WindowModal);
    QtBindLocalText(this, "QtRecentPlacesDialog",
                    QT_TRANSLATE_NOOP("QtRecentPlacesDialog", "Recent places"));
    search_->setObjectName("recentPlacesSearch");
    QtBindLocalText(search_, "QtRecentPlacesDialog",
                    QT_TRANSLATE_NOOP("QtRecentPlacesDialog", "Search by name or path"), true);
    search_->setPlaceholderText(tr("Search by name or path"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    list_->setObjectName("foldersHistoryList");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setMinimumSize(160, 100);
    list_->setSpacing(4);
    list_->installEventFilter(this);
    list_->viewport()->installEventFilter(this);
    empty_->setObjectName("recentPlacesEmpty");
    empty_->setWordWrap(true);
    error_->setObjectName("recentPlacesError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->hide();
    auto *actions = new QHBoxLayout;
    remove_->setObjectName("recentPlacesRemove");
    clear_->setObjectName("recentPlacesClear");
    remove_->setAutoDefault(false);
    clear_->setAutoDefault(false);
    QtBindLocalText(remove_, "QtRecentPlacesDialog",
                    QT_TRANSLATE_NOOP("QtRecentPlacesDialog", "Remove from list"));
    QtBindLocalText(clear_, "QtRecentPlacesDialog",
                    QT_TRANSLATE_NOOP("QtRecentPlacesDialog", "Clear list"));
    actions->addWidget(remove_);
    actions->addWidget(clear_);
    actions->addStretch();
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Open, this);
    openButton_ = buttons->button(QDialogButtonBox::Open);
    openButton_->setObjectName("recentPlacesOpen");
    QtBindLocalText(openButton_, "DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Open"));
    openButton_->setDefault(true);
    auto *body = QtCreateDialogLayout(*this, *buttons);
    body->addWidget(search_);
    body->addWidget(list_, 1);
    body->addWidget(empty_);
    body->addLayout(actions);
    body->addWidget(error_);
    connect(search_, &QLineEdit::textChanged, this, [this] {
        error_->hide();
        Rebuild();
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { accept(); });
    connect(list_, &QListWidget::currentItemChanged, this, [this] {
        error_->hide();
        UpdateSelection();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QtRecentPlacesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(remove_, &QPushButton::clicked, this, [this] {
        error_->hide();
        if (auto *item = list_->currentItem())
            store_.Remove(item->data(Qt::UserRole).toString());
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        error_->hide();
        store_.Clear();
    });
    connect(&store_, &QAbstractItemModel::modelReset, this, [this] {
        error_->hide();
        Rebuild();
    });
    Rebuild();
    resize(600, 480);
    QtNormalizeDialogSize(*this);
    search_->setFocus();
}

void QtRecentPlacesDialog::SetOpenHandler(std::function<void(const QtRecentPlace &)> open) {
    open_ = std::move(open);
}

void QtRecentPlacesDialog::Rebuild() {
    const QString previous =
        list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    const int oldRow = list_->currentRow();
    const int previousIndex = oldRow >= 0 ? store_.IndexOf(previous) : -1;
    const QSignalBlocker blocked(list_);
    list_->clear();
    int selected = -1;
    for (int row = 0; row < store_.rowCount(); ++row) {
        const auto place = store_.Place(row);
        const QString name = Name(place);
        if (!name.contains(search_->text(), Qt::CaseInsensitive) &&
            !place.path.contains(search_->text(), Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, place.path);
        item->setData(Qt::AccessibleTextRole, name + '\n' + place.path);
        const auto icon = place.nativeIdentity == "computer"     ? QtCommandIcon::Computer
                          : place.nativeIdentity == "network"    ? QtCommandIcon::Network
                          : place.archive.physicalPath.isEmpty() ? QtCommandIcon::Folder
                                                                 : QtCommandIcon::Archive;
        auto *summary = new QtSourceSummary(QtCommandSymbol(icon), name,
                                            place.path.isEmpty() ? name : place.path, list_);
        summary->setAttribute(Qt::WA_TransparentForMouseEvents);
        list_->setItemWidget(item, summary);
        if (row == previousIndex)
            selected = list_->count() - 1;
    }
    if (list_->count())
        list_->setCurrentRow(selected >= 0 ? selected : 0);
    empty_->setText(store_.rowCount() == 0 ? tr("No recent places yet.")
                                           : tr("No matching places."));
    empty_->setVisible(list_->count() == 0);
    list_->setVisible(list_->count() != 0);
    ResizeRows();
    UpdateSelection();
}

void QtRecentPlacesDialog::UpdateSelection() {
    for (int row = 0; row < list_->count(); ++row)
        if (auto *summary = static_cast<QtSourceSummary *>(list_->itemWidget(list_->item(row))))
            summary->SetSelected(row == list_->currentRow());
    openButton_->setEnabled(!busy_ && list_->currentItem());
    remove_->setEnabled(!busy_ && list_->currentItem());
    clear_->setEnabled(!busy_ && store_.rowCount());
}

void QtRecentPlacesDialog::ResizeRows() {
    const int width = qMax(80, list_->viewport()->width() - 2 * list_->spacing());
    for (int row = 0; row < list_->count(); ++row) {
        auto *item = list_->item(row);
        auto *summary = static_cast<QtSourceSummary *>(list_->itemWidget(item));
        if (!summary)
            continue;
        item->setSizeHint(QSize(width, qMax(60, summary->heightForWidth(width))));
    }
}

void QtRecentPlacesDialog::accept() {
    if (busy_ || !list_->currentItem() || !open_)
        return;
    const QString path = list_->currentItem()->data(Qt::UserRole).toString();
    for (int row = 0; row < store_.rowCount(); ++row) {
        const auto place = store_.Place(row);
        if (place.path != path)
            continue;
        busy_ = true;
        search_->setEnabled(false);
        list_->setEnabled(false);
        error_->hide();
        UpdateSelection();
        open_(place);
        return;
    }
}

void QtRecentPlacesDialog::Complete(bool success, const QString &error) {
    if (success) {
        QDialog::accept();
        return;
    }
    busy_ = false;
    search_->setEnabled(true);
    list_->setEnabled(true);
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
    UpdateSelection();
}

bool QtRecentPlacesDialog::eventFilter(QObject *object, QEvent *event) {
    if (object == list_->viewport() && event->type() == QEvent::Resize && !rowResizePending_) {
        rowResizePending_ = true;
        QTimer::singleShot(0, this, [this] {
            rowResizePending_ = false;
            ResizeRows();
        });
    }
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (object == search_ && key->key() == Qt::Key_Down && list_->count()) {
            list_->setFocus();
            return true;
        }
        if ((object == list_ || object == search_) &&
            (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            accept();
            return true;
        }
    }
    return QDialog::eventFilter(object, event);
}

void QtRecentPlacesDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        search_->setPlaceholderText(tr("Search by name or path"));
        Rebuild();
    }
}
