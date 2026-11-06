#include "ui/controls/qtmetadatarows.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QGridLayout>
#include <QLabel>

QtMetadataRows::QtMetadataRows(QWidget *parent) : QWidget(parent), rows_(new QGridLayout(this)) {
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setHorizontalSpacing(12);
    rows_->setVerticalSpacing(8);
    rows_->setColumnStretch(1, 1);
}

QLabel *QtMetadataRows::AddRow(const char *context, const char *caption, const QString &value,
                               const QString &name) {
    const auto row = AddLiteralRow({}, value, name);
    QtBindLocalText(row.caption, context, caption);
    return row.value;
}

QtMetadataRows::Row QtMetadataRows::AddLiteralRow(const QString &caption, const QString &value,
                                                  const QString &name) {
    auto *label = new QtWrappingLabel(this);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setText(caption);
    auto *text = new QtWrappingLabel(value, this);
    text->setObjectName(name);
    text->setAccessibleName(caption);
    text->setTextFormat(Qt::PlainText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    text->setWordWrap(true);
    text->setMinimumWidth(0);
    auto policy = text->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    text->setSizePolicy(policy);
    text->installEventFilter(this);
    label->setBuddy(text);
    const int row = rows_->rowCount();
    rows_->addWidget(label, row, 0, Qt::AlignTop);
    rows_->addWidget(text, row, 1);
    return {label, text};
}

bool QtMetadataRows::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::Resize)
        if (auto *value = qobject_cast<QLabel *>(object))
            value->setMinimumHeight(value->heightForWidth(value->width()));
    return QWidget::eventFilter(object, event);
}
