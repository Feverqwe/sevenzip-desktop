#include "dialogs/licenses/qtlicensesdialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/reports/qttextreport.h"
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

QtLicensesDialog::QtLicensesDialog(QWidget *parent, const QString &directory)
    : QDialog(parent), documents_(directory) {
    setObjectName("licensesDialog");
    QtUseContextDialogModality(*this);
    QtBindLocalText(this, "LicensesDialog",
                    QT_TRANSLATE_NOOP("LicensesDialog", "Licenses and notices"));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close, this);
    copy_ = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    copy_->setObjectName("licensesCopy");
    copy_->setAutoDefault(false);
    QtBindLocalText(copy_, "LicensesDialog", QT_TRANSLATE_NOOP("LicensesDialog", "Copy text"));
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    search_ = new QLineEdit(this);
    search_->setObjectName("licensesSearch");
    search_->setClearButtonEnabled(true);
    search_->setProperty("languagePlaceholder", true);
    QtBindLocalText(search_, "LicensesDialog",
                    QT_TRANSLATE_NOOP("LicensesDialog", "Search components or licenses"), true);
    search_->installEventFilter(this);
    layout->addWidget(search_);
    message_ = new QtWrappingLabel(this);
    message_->setObjectName("licensesMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    splitter_ = new QSplitter(this);
    splitter_->setObjectName("licensesSplitter");
    splitter_->setChildrenCollapsible(false);
    list_ = new QListWidget(splitter_);
    list_->setObjectName("licensesDocuments");
    list_->setWordWrap(true);
    list_->setTextElideMode(Qt::ElideNone);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QtBindLocalText(list_, "LicensesDialog", QT_TRANSLATE_NOOP("LicensesDialog", "Documents"),
                    true);
    text_ = new QPlainTextEdit(splitter_);
    text_->setObjectName("licensesText");
    QtConfigureTextReport(*text_, font());
    text_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QtBindLocalText(text_, "LicensesDialog", QT_TRANSLATE_NOOP("LicensesDialog", "License text"),
                    true);
    layout->addWidget(splitter_, 1);
    connect(search_, &QLineEdit::textChanged, this, &QtLicensesDialog::Filter);
    connect(list_, &QListWidget::currentRowChanged, this, &QtLicensesDialog::ShowDocument);
    connect(copy_, &QPushButton::clicked, this, [this] { QtCopyFullTextReport(documentText_); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    documents_.load(indexError_);
    search_->setEnabled(indexError_.isEmpty());
    Filter();
    resize(860, 600);
    setMinimumSize(QSize(320, 320).expandedTo(minimumSizeHint()));
    UpdateOrientation();
    search_->setFocus();
    QWidget::setTabOrder({search_, list_, text_, copy_, buttons->button(QDialogButtonBox::Close)});
}

void QtLicensesDialog::Filter() {
    const int previous =
        list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toInt() : -1;
    int selected = -1;
    {
        QSignalBlocker block(list_);
        list_->clear();
        const QString query = search_->text().trimmed();
        const auto &documents = documents_.documents();
        for (int row = 0; row < documents.size(); ++row) {
            const auto &document = documents[row];
            if (!document.title.contains(query, Qt::CaseInsensitive) &&
                !document.path.contains(query, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(document.title, list_);
            item->setData(Qt::UserRole, row);
            item->setToolTip(document.path);
            if (row == previous)
                selected = list_->count() - 1;
        }
    }
    list_->setCurrentRow(selected >= 0 ? selected : list_->count() > 0 ? 0 : -1);
    ShowDocument();
}

void QtLicensesDialog::ShowDocument() {
    documentText_.clear();
    text_->clear();
    copy_->setEnabled(false);
    QString error = indexError_;
    if (error.isEmpty()) {
        if (const auto *item = list_->currentItem()) {
            const auto text = documents_.read(item->data(Qt::UserRole).toInt(), error);
            if (text) {
                documentText_ = *text;
                text_->setPlainText(*text);
                copy_->setEnabled(true);
            }
        } else {
            error = documents_.documents().isEmpty()
                        ? QCoreApplication::translate("LicensesDialog",
                                                      "No license documents in this package.")
                        : QCoreApplication::translate("LicensesDialog",
                                                      "No documents match your search.");
        }
    }
    message_->setText(error);
    message_->setVisible(!error.isEmpty());
}

void QtLicensesDialog::UpdateOrientation() {
    if (!splitter_)
        return;
    const Qt::Orientation orientation = width() < 680 ? Qt::Vertical : Qt::Horizontal;
    if (splitter_->orientation() != orientation) {
        splitter_->setOrientation(orientation);
        splitter_->setSizes(orientation == Qt::Vertical ? QList<int>{150, 350}
                                                        : QList<int>{240, 540});
    }
}

void QtLicensesDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    UpdateOrientation();
}

void QtLicensesDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && list_) {
        if (!indexError_.isEmpty()) {
            documents_.load(indexError_);
            search_->setEnabled(indexError_.isEmpty());
            Filter();
        } else
            ShowDocument();
    }
}

bool QtLicensesDialog::eventFilter(QObject *object, QEvent *event) {
    if (object == search_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return ||
            key->key() == Qt::Key_Enter) {
            list_->setFocus();
            return true;
        }
    }
    return QDialog::eventFilter(object, event);
}
