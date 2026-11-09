#include "ui/controls/qtinlinefoldername.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QKeyEvent>
#include <QLineEdit>
#include <QVBoxLayout>
#include <utility>

QtInlineFolderName::QtInlineFolderName(QWidget *parent)
    : QWidget(parent), name_(new QLineEdit(this)), error_(new QtWrappingLabel(this)) {
    setObjectName("inlineCreateFolder");
    auto *row = new QVBoxLayout(this);
    row->setContentsMargins(12, 6, 12, 6);
    name_->setObjectName("inlineFolderName");
    name_->setProperty("languagePlaceholder", true);
    QtBindLocalText(name_, "InlineCreateFolder",
                    QT_TRANSLATE_NOOP("InlineCreateFolder", "Folder name"), true);
    row->addWidget(name_);
    error_->setObjectName("inlineFolderError");
    error_->setTextFormat(Qt::PlainText);
    error_->hide();
    row->addWidget(error_);
    name_->installEventFilter(this);
    connect(name_, &QLineEdit::textEdited, this, &QtInlineFolderName::ClearError);
}

void QtInlineFolderName::OnCancel(std::function<void()> cancel) { cancel_ = std::move(cancel); }

bool QtInlineFolderName::eventFilter(QObject *watched, QEvent *event) {
    if (watched != name_ ||
        (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress))
        return QWidget::eventFilter(watched, event);
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter && (key != Qt::Key_Escape || !cancel_))
        return QWidget::eventFilter(watched, event);
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        if (key == Qt::Key_Escape)
            cancel_();
        else
            name_->returnPressed();
    }
    return true;
}

void QtInlineFolderName::ClearError() { error_->hide(); }

void QtInlineFolderName::ShowError(const QString &message, bool focusName) {
    error_->setText(message);
    error_->show();
    if (focusName) {
        name_->setFocus();
        name_->selectAll();
    }
}
