#include "filemanager/views/qtworkspacefiletable.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/qtworkspacetheme.h"
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMimeType>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSettings>

namespace {
bool CommonFiles(const CPanel &panel) {
    return panel.IsFSFolder() || panel.IsArcFolder() || panel.IsAltStreamsFolder();
}
} // namespace

QtWorkspaceFileTable::QtWorkspaceFileTable(const QString &settingsPath, QWidget *parent)
    : FileTable(7, parent), settingsPath_(settingsPath) {
    QtWorkspaceTheme::fileView(*this);
    QtWorkspaceTheme::header(*horizontalHeader());
    verticalHeader()->setDefaultSectionSize(QtWorkspaceTheme::metric(*this, 28));
    setItemDelegate(CreateFileRowDelegate(this, true));
    setTextElideMode(Qt::ElideRight);
    setShowGrid(false);
    connect(selectionModel(), &QItemSelectionModel::currentRowChanged, viewport(),
            [this] { viewport()->update(); });
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, viewport(),
            [this] { viewport()->update(); });
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int column) {
        if (configuring_ || group_.isEmpty())
            return;
        if (column == columnForProperty(kpidName))
            autoName_ = false;
        SavePresentation();
    });
    connect(horizontalHeader(), &QHeaderView::sectionMoved, this, [this] {
        if (!configuring_)
            SavePresentation();
    });
}

void QtWorkspaceFileTable::DefaultColumns(const CPanel &panel, CPropColumns &columns) {
    if (!CommonFiles(panel))
        return;
    FOR_VECTOR(i, columns) {
        auto &column = columns[i];
        column.IsVisible = !column.IsRawProp && (column.ID == kpidName || column.ID == kpidSize ||
                                                 column.ID == kpidMTime || column.ID == kpidType ||
                                                 column.ID == kpidExtension);
        if (column.IsVisible)
            column.Width = column.ID == kpidName ? 260 : column.ID == kpidMTime ? 190 : 112;
    }
}

void QtWorkspaceFileTable::configureSourceColumns() {
    const QScopedValueRollback<bool> configuring(configuring_, true);
    const bool saved = sourcePanel->HasSavedColumnLayout();
    group_ =
        QStringLiteral("workspace/columns/%1/")
            .arg(QString::fromLatin1(ToQString(sourcePanel->GetFolderTypeID()).toUtf8().toHex()));
    FileTable::configureSourceColumns();
    horizontalHeader()->setVisible(!sourcePanel->GetVisibleColumns().IsEmpty());
    const auto &catalog = sourcePanel->GetColumns();
    typeAvailable_ = CommonFiles(*sourcePanel) && catalog.FindItem_for_PropID(kpidType) < 0 &&
                     catalog.FindItem_for_PropID(kpidExtension) < 0;
    QSettings settings(settingsPath_, QSettings::IniFormat);
    const bool automatic =
        !saved && (CommonFiles(*sourcePanel) || sourcePanel->GetVisibleColumns().Size() == 1);
    autoName_ = settings.value(group_ + "autoName", automatic).toBool();
    if (typeAvailable_) {
        setColumnHidden(5, !settings.value(group_ + "typeVisible", !saved).toBool());
        setColumnWidth(
            5, qMax(32, settings.value(group_ + "typeWidth", QtWorkspaceTheme::metric(*this, 132))
                            .toInt()));
        RestoreTypeOrder(settings.value(group_ + "typeOrder", 2).toInt());
    }
    if (!saved && CommonFiles(*sourcePanel)) {
        int position = 0;
        for (const PROPID property : {kpidName, kpidSize, kpidType, kpidExtension, kpidMTime}) {
            const int column = columnForProperty(property);
            if (column >= 0 && !isColumnHidden(column))
                horizontalHeader()->moveSection(horizontalHeader()->visualIndex(column),
                                                position++);
        }
    }
    RefreshTypeCaption();
    FitName();
    SavePresentation();
}

void QtWorkspaceFileTable::RefreshTypeCaption() {
    if (typeAvailable_)
        panelModel()->setHeaderData(5, Qt::Horizontal, tr("Type"));
}

void QtWorkspaceFileTable::invalidateLanguageText() {
    FileTable::invalidateLanguageText();
    RefreshTypeCaption();
}

void QtWorkspaceFileTable::RestoreTypeOrder(int position) {
    auto *header = horizontalHeader();
    int destination = header->count();
    int visible = 0;
    for (int visual = 0; visual < header->count(); ++visual) {
        const int column = header->logicalIndex(visual);
        if (column != 5 && !isColumnHidden(column) && visible++ >= qMax(0, position)) {
            destination = visual;
            break;
        }
    }
    const int from = header->visualIndex(5);
    if (from < destination)
        --destination;
    header->moveSection(from, qBound(0, destination, header->count() - 1));
}

QString QtWorkspaceFileTable::typeLabel(const QString &name, bool directory) const {
    if (!typeAvailable_)
        return FileTable::typeLabel(name, directory);
    if (directory)
        return tr("Folder");
    const auto type = mimeTypes_.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    if (type.isDefault())
        return tr("File");
    const QString comment = type.comment();
    if (!comment.isEmpty() && comment != type.name())
        return comment;
    if (type.inherits(QStringLiteral("text/plain")))
        return tr("Text document");
    const QString extension = QFileInfo(name).suffix().toUpper();
    return extension.isEmpty() ? tr("File") : tr("%1 file").arg(extension);
}

void QtWorkspaceFileTable::appendPresentationColumnActions(QMenu &menu) {
    if (!typeAvailable_)
        return;
    menu.addSeparator();
    auto *type = menu.addAction(tr("Type"));
    type->setCheckable(true);
    type->setChecked(!isColumnHidden(5));
    connect(type, &QAction::toggled, this, [this](bool visible) {
        setColumnHidden(5, !visible);
        if (!visible)
            restoreVisibleCurrentColumn();
        if (!visible && horizontalHeader()->sortIndicatorSection() == 5)
            sortByColumn(columnForProperty(kpidName), Qt::AscendingOrder);
        FitName();
        SavePresentation();
    });
}

void QtWorkspaceFileTable::SavePresentation() {
    if (group_.isEmpty())
        return;
    QSettings settings(settingsPath_, QSettings::IniFormat);
    settings.setValue(group_ + "autoName", autoName_);
    if (typeAvailable_) {
        settings.setValue(group_ + "typeVisible", !isColumnHidden(5));
        settings.setValue(group_ + "typeWidth", columnWidth(5));
        int position = 0;
        for (int visual = 0; visual < horizontalHeader()->visualIndex(5); ++visual)
            if (!isColumnHidden(horizontalHeader()->logicalIndex(visual)))
                ++position;
        settings.setValue(group_ + "typeOrder", position);
    }
}

void QtWorkspaceFileTable::sourceColumnVisibilityChanged() {
    FitName();
    SavePresentation();
}

void QtWorkspaceFileTable::FitName() {
    if (!autoName_ || group_.isEmpty())
        return;
    const QScopedValueRollback<bool> configuring(configuring_, true);
    const int name = columnForProperty(kpidName);
    if (name < 0)
        return;
    int metadata = 0;
    for (int column = 0; column < columnCount(); ++column)
        if (column != name && !isColumnHidden(column)) {
            // Keep complete leading metadata columns at compact widths;
            // remaining columns are reached by ordinary horizontal scrolling.
            if (propertyId(column) == kpidMTime && viewport()->width() < 650)
                continue;
            if ((propertyId(column) == kpidType || propertyId(column) == kpidExtension) &&
                viewport()->width() < 440)
                continue;
            metadata += columnWidth(column);
        }
    setColumnWidth(name,
                   qMax(QtWorkspaceTheme::metric(*this, 132), viewport()->width() - metadata));
}

void QtWorkspaceFileTable::resizeEvent(QResizeEvent *event) {
    FileTable::resizeEvent(event);
    FitName();
}

void QtWorkspaceFileTable::paintEvent(QPaintEvent *event) {
    FileTable::paintEvent(event);
    QPainter painter(viewport());
    const int slack = horizontalHeader()->length() - horizontalScrollBar()->value();
    if (slack < viewport()->width())
        for (int row = qMax(0, rowAt(0)); row < rowCount(); ++row) {
            const int top = rowViewportPosition(row);
            if (top >= viewport()->height())
                break;
            if (selectionModel()->isRowSelected(row, {}))
                painter.fillRect(QRect(qMax(0, slack), top, viewport()->width() - qMax(0, slack),
                                       rowHeight(row)),
                                 palette().color(QPalette::Highlight));
        }
    if (!hasFocus() || !currentIndex().isValid())
        return;
    const QRect item =
        visualRect(currentIndex().siblingAtColumn(qMax(0, columnForVisibleProperty(0))));
    if (!item.isValid() || item.bottom() < 0 || item.top() >= viewport()->height())
        return;
    painter.setPen(QtWorkspaceTheme::colors().accent);
    const QRect row(1, item.top() + 1, viewport()->width() - 3, item.height() - 3);
    painter.drawRect(row);
}

void QtWorkspaceFileTable::scrollContentsBy(int dx, int dy) {
    FileTable::scrollContentsBy(dx, dy);
    viewport()->update();
}

void QtWorkspaceFileTable::focusInEvent(QFocusEvent *event) {
    FileTable::focusInEvent(event);
    viewport()->update();
}

void QtWorkspaceFileTable::focusOutEvent(QFocusEvent *event) {
    FileTable::focusOutEvent(event);
    viewport()->update();
}

void QtWorkspaceFileTable::changeEvent(QEvent *event) {
    FileTable::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        RefreshTypeCaption();
}
