#include "localization/qtlangcontrols.h"
#include "settings/MenuPage.h"
#include "settings/SystemPage.h"
#include "settings/qtfmoptionsdialog.h"
#include "ui/controls/qtdisclosure.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

void QtFmOptionsDialog::buildNavigation() {
    _sections->tabBar()->hide();
    _sections->setFocusPolicy(Qt::NoFocus);
    _sections->tabBar()->setFocusPolicy(Qt::NoFocus);
    _sections->setDocumentMode(true);

    _navigationPanel = new QWidget(this);
    _navigationPanel->setObjectName("settingsSidebar");
    auto *navigationLayout = new QVBoxLayout(_navigationPanel);
    navigationLayout->setContentsMargins(10, 16, 10, 20);
    _navigation = new QListWidget(_navigationPanel);
    _navigation->setObjectName("settingsNavigation");
    _navigation->setFrameShape(QFrame::NoFrame);
    _navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _navigation->setWordWrap(true);
    _navigation->setSpacing(4);
    QtBindLocalText(_navigation, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Settings sections"), true);
    _navigationPanel->setBackgroundRole(QPalette::Window);
    _navigationPanel->setAutoFillBackground(true);
    navigationLayout->addWidget(_navigation);
    _compactNavigationPanel = new QWidget(this);
    auto *compactLayout = new QVBoxLayout(_compactNavigationPanel);
    compactLayout->setContentsMargins(16, 10, 16, 10);
    _compactNavigation = new QListWidget(_compactNavigationPanel);
    compactLayout->addWidget(_compactNavigation);
    _compactNavigation->setObjectName("settingsSectionChoice");
    _compactNavigation->setFrameShape(QFrame::NoFrame);
    _compactNavigation->setFlow(QListView::LeftToRight);
    _compactNavigation->setWrapping(true);
    _compactNavigation->setResizeMode(QListView::Adjust);
    _compactNavigation->setTextElideMode(Qt::ElideNone);
    _compactNavigation->setSpacing(4);
    _compactNavigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QtBindLocalText(_compactNavigation, "SettingsRedesign",
                    QT_TRANSLATE_NOOP("SettingsRedesign", "Settings sections"), true);
    for (int i = 0; i < _sections->count(); ++i) {
        _navigation->addItem(_sections->tabText(i));
        _compactNavigation->addItem(_sections->tabText(i));
    }
    connect(_navigation, &QListWidget::currentRowChanged, _sections, &QTabWidget::setCurrentIndex);
    connect(_compactNavigation, &QListWidget::currentRowChanged, _sections,
            &QTabWidget::setCurrentIndex);
    connect(_sections, &QTabWidget::currentChanged, this, [this](int index) {
        const QSignalBlocker side(_navigation), compact(_compactNavigation);
        _navigation->setCurrentRow(index);
        _compactNavigation->setCurrentRow(index);
    });
    _navigation->setCurrentRow(0);
    _compactNavigation->setCurrentRow(0);
    _compactNavigationPanel->hide();
    _navigationDivider = new QFrame(this);
    _navigationDivider->setFrameShape(QFrame::VLine);
    _frame = new QGridLayout(this);
    _frame->setContentsMargins(0, 0, 0, 0);
    _frame->setSpacing(0);
    _frame->addWidget(_navigationPanel, 0, 0, 2, 1);
    _frame->addWidget(_navigationDivider, 0, 1, 2, 1);
    _frame->addWidget(_sections, 0, 2);
    _footer = new QWidget(this);
    _footer->setObjectName("settingsFooter");
    auto *footer = new QHBoxLayout(_footer);
    footer->setContentsMargins(24, 12, 24, 20);
    footer->setSpacing(16);
    footer->addWidget(_helpButtons);
    footer->addStretch();
    footer->addWidget(_buttons);
    _frame->addWidget(_footer, 1, 2);
    _frame->setColumnStretch(2, 1);
    _frame->setRowStretch(0, 1);
}

void QtFmOptionsDialog::reloadLangItems() {
    QtReloadLangTexts(*this);
    _menuPage->ReloadLangItems();
    _systemPage->ReloadLangItems();
    auto *pages = findChild<QTabWidget *>("optionsPages");
    for (int i = 0; i < pages->count(); i++) {
        pages->setTabText(
            i, QCoreApplication::translate(
                   "SettingsRedesign",
                   pages->widget(i)->property("sectionTitle").toByteArray().constData()));
        _navigation->item(i)->setText(pages->tabText(i));
        _compactNavigation->item(i)->setText(pages->tabText(i));
    }
    refreshSectionSummaries();
}
void QtFmOptionsDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updateNavigationGeometry();
}

void QtFmOptionsDialog::updateNavigationGeometry() {
    if (!_frame)
        return;
    auto sidebarPalette = palette();
    const QColor background = sidebarPalette.color(QPalette::Window);
    const QColor sidebar = background.darker(background.lightness() < 128 ? 110 : 103);
    sidebarPalette.setColor(QPalette::Window, sidebar);
    sidebarPalette.setColor(QPalette::Base, sidebar);
    _navigationPanel->setPalette(sidebarPalette);
    _navigation->setPalette(sidebarPalette);
    _navigationPanel->setFixedWidth(fontMetrics().averageCharWidth() * 22);
    for (int i = 0; i < _navigation->count(); ++i) {
        const QRect label =
            fontMetrics().boundingRect(QRect(0, 0, _navigationPanel->width() - 36, 10000),
                                       Qt::TextWordWrap, _navigation->item(i)->text());
        _navigation->item(i)->setSizeHint(QSize(0, label.height() + 16));
    }
    const int cellHeight = fontMetrics().height() + 16;
    const int available = qMax(1, width() - 32);
    int rows = 1, used = 0;
    for (int i = 0; i < _compactNavigation->count(); ++i) {
        auto *item = _compactNavigation->item(i);
        const int cellWidth = qMin(available, fontMetrics().horizontalAdvance(item->text()) + 24);
        item->setSizeHint(QSize(cellWidth, cellHeight));
        if (used && used + cellWidth + 8 > available) {
            ++rows;
            used = 0;
        }
        used += cellWidth + 8;
    }
    _compactNavigation->setFixedHeight(rows * (cellHeight + 8));
    const bool compact = width() <= qMax(560, fontMetrics().averageCharWidth() * 80);
    if (compact == _compact)
        return;
    _compact = compact;
    for (auto *widget : {_navigationPanel, static_cast<QWidget *>(_navigationDivider),
                         _compactNavigationPanel, static_cast<QWidget *>(_sections), _footer})
        _frame->removeWidget(widget);
    _navigationPanel->setVisible(!compact);
    _navigationDivider->setVisible(!compact);
    _compactNavigationPanel->setVisible(compact);
    if (compact) {
        _frame->addWidget(_compactNavigationPanel, 0, 0, 1, 3);
        _frame->addWidget(_sections, 1, 0, 1, 3);
        _frame->addWidget(_footer, 2, 0, 1, 3);
    } else {
        _frame->addWidget(_navigationPanel, 0, 0, 2, 1);
        _frame->addWidget(_navigationDivider, 0, 1, 2, 1);
        _frame->addWidget(_sections, 0, 2);
        _frame->addWidget(_footer, 1, 2);
    }
    _frame->setRowStretch(0, compact ? 0 : 1);
    _frame->setRowStretch(1, compact ? 1 : 0);
}

void QtFmOptionsDialog::revealControl(QWidget *control) {
    for (auto *parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto *disclosure = qobject_cast<QtDisclosure *>(parent))
            disclosure->setExpanded(true);
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            if (_sections->indexOf(scroll) >= 0)
                _sections->setCurrentWidget(scroll);
            scroll->ensureWidgetVisible(control);
        }
    }
    control->setFocus(Qt::OtherFocusReason);
}
