#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

QtSourceSummary::QtSourceSummary(const QIcon &icon, const QString &name, const QString &path,
                                 QWidget *parent)
    : QWidget(parent), name_(new QLabel(name, this)), path_(new QtWrappingLabel(path, this)) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);
    auto *symbol = new QLabel(this);
    symbol->setPixmap(icon.pixmap(24, 24));
    symbol->setFixedSize(24, 24);
    row->addWidget(symbol, 0, Qt::AlignTop);
    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(4);
    for (auto *label : {name_, path_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setMinimumWidth(0);
        text->addWidget(label);
    }
    QFont title = name_->font();
    title.setWeight(QFont::DemiBold);
    name_->setFont(title);
    row->addLayout(text, 1);
    setToolTip(path);
    setAccessibleName(name);
    setAccessibleDescription(path);
    SetSelected(false);
}

void QtSourceSummary::SetSelected(bool selected) {
    name_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::Text);
    path_->setForegroundRole(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
}

int QtSourceSummary::heightForWidth(int width) const {
    return layout()->totalHeightForWidth(width);
}
