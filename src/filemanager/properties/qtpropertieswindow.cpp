#include "filemanager/properties/qtpropertieswindow.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
using Scope = QtPropertyReport::Scope;
QString text(const char *source) { return QCoreApplication::translate("PropertiesWindow", source); }
QString available(const QString &value) {
    return value.isEmpty() ? text(QT_TRANSLATE_NOOP("PropertiesWindow", "Not available")) : value;
}
QString scopeName(const QtPropertyReport::Group &group) {
    switch (group.scope) {
    case Scope::Selection:
        return "propertiesSelection";
    case Scope::Folder:
        return "propertiesFolder";
    case Scope::Archive:
        return QString("propertiesArchive%1").arg(group.level);
    case Scope::ArchiveLink:
        return QString("propertiesArchiveEntry%1").arg(group.level);
    case Scope::UnopenedArchive:
        return "propertiesUnopenedArchive";
    }
    return {};
}
} // namespace

QtPropertiesWindow::QtPropertiesWindow(QtPropertyReport report, QWidget *parent,
                                       QWidget *returnFocus)
    : QDialog(parent, Qt::Window), report_(std::move(report)), returnFocus_(returnFocus) {
    setObjectName("selectedPropertiesDialog");
    setWindowModality(Qt::NonModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindLangText(this, IDS_PROPERTIES);
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName("propertiesActions");
    auto *copy = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    copy->setObjectName("propertiesCopyInformation");
    copy->setAutoDefault(false);
    QtBindLocalText(copy, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Copy information"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(Information()); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    const bool directory =
        report_.items.isEmpty() ||
        (report_.items.size() == 1 && report_.items.first().directory.value_or(false));
    summary_ = new QtSourceSummary(
        style()->standardIcon(directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon), {}, {}, this);
    summary_->setObjectName("propertiesIdentity");
    summary_->SetSelectable(true);
    layout->addWidget(summary_);
    auto *primary = new QtMetadataRows(this);
    primary->setObjectName("propertiesPrimary");
    location_ =
        primary->AddRow("PropertiesWindow", QT_TRANSLATE_NOOP("PropertiesWindow", "Location"), {},
                        "propertiesLocation");
    size_ = primary->AddRow("PropertiesWindow", QT_TRANSLATE_NOOP("PropertiesWindow", "Size"), {},
                            "propertiesSize");
    modified_ =
        primary->AddRow("PropertiesWindow", QT_TRANSLATE_NOOP("PropertiesWindow", "Modified"), {},
                        "propertiesModified");
    layout->addWidget(primary);
    if (report_.items.size() > 1) {
        QStringList names;
        for (const auto &item : report_.items)
            names.append(item.name);
        auto *list = new QtWrappingLabel(names.join('\n'), this);
        selectedNames_ = list;
        list->setObjectName("propertiesSelectedNames");
        list->setTextFormat(Qt::PlainText);
        list->setWordWrap(true);
        list->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        auto *section = new QtDisclosure(list, this);
        section->setObjectName("propertiesSelectedObjects");
        QtBindLocalText(section->button(), "PropertiesWindow",
                        QT_TRANSLATE_NOOP("PropertiesWindow", "Selected objects"));
        layout->addWidget(section);
    }
    for (qsizetype i = 0; i < report_.groups.size(); ++i) {
        BuildGroup(i, false, *layout);
        BuildGroup(i, true, *layout);
    }
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    buttons->button(QDialogButtonBox::Close)->setFocus();
    Refresh();
    resize(520, 440);
    QtFitScrollableDialog(*this);
    initialFrame_ = size();
}

void QtPropertiesWindow::BuildGroup(qsizetype index, bool comments, QVBoxLayout &layout) {
    const auto &group = report_.groups[index];
    auto *values = new QtMetadataRows(this);
    values->setObjectName(scopeName(group) + (comments ? "Comments" : ""));
    int count = 0;
    for (const auto row : group.rows) {
        if (report_.selectionSummaryRow == row)
            continue; // SourceSummary already owns the selected-object count.
        if ((report_.PropertyID(row) == kpidComment) != comments)
            continue;
        const auto controls = values->AddLiteralRow(Caption(row, group.scope), Value(row));
        controls.value->setObjectName(QString("propertyValue%1").arg(row));
        rows_.append({row, group.scope, controls});
        ++count;
    }
    if (!count) {
        delete values;
        return;
    }
    auto *section = new QtDisclosure(values, this);
    section->setObjectName(values->objectName() + "Disclosure");
    sections_.append({index, comments, section});
    layout.addWidget(section);
}

QString QtPropertiesWindow::SummaryName() const {
    if (report_.items.size() == 1)
        return available(report_.items.first().name);
    if (report_.items.size() > 1)
        return QtSelectedItemsText(int(report_.items.size()));
    const QString name = QFileInfo(QDir::cleanPath(report_.location)).fileName();
    return name.isEmpty() ? available(report_.location) : name;
}
QString QtPropertiesWindow::SummaryType() const {
    if (report_.items.size() > 1) {
        int folders = 0;
        for (const auto &item : report_.items) {
            if (!item.directory)
                return QtSelectedItemsText(int(report_.items.size()));
            folders += *item.directory;
        }
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Files: %1 · Folders: %2"))
            .arg(report_.items.size() - folders)
            .arg(folders);
    }
    if (report_.items.isEmpty() || report_.items.first().directory.value_or(false))
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Folder"));
    for (const auto &group : report_.groups)
        if (group.scope == Scope::Selection) {
            const QString type = report_.Property(group, kpidType);
            if (!type.isEmpty())
                return type;
        }
    if (!report_.items.first().directory)
        return available({});
    for (const auto &group : report_.groups)
        if (group.scope == Scope::Archive)
            return text(QT_TRANSLATE_NOOP("PropertiesWindow", "File inside archive"));
    return text(QT_TRANSLATE_NOOP("PropertiesWindow", "File"));
}
QString QtPropertiesWindow::SummarySize() const {
    if (report_.items.size() != 1 || report_.items.first().directory.value_or(false))
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Not calculated"));
    for (const auto &group : report_.groups)
        if (group.scope == Scope::Selection)
            return available(report_.Property(group, kpidSize));
    return available({});
}
QString QtPropertiesWindow::SummaryModified() const {
    if (report_.items.size() > 1)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Not applicable"));
    const auto scope = report_.items.isEmpty() ? Scope::Folder : Scope::Selection;
    for (const auto &group : report_.groups)
        if (group.scope == scope)
            return available(report_.Property(group, kpidMTime));
    return available({});
}
QString QtPropertiesWindow::SummaryLocation() const {
    if (report_.items.size() == 1 && !report_.items.first().prefix.isEmpty())
        return QDir(report_.location).filePath(report_.items.first().prefix);
    return available(report_.location);
}
QString QtPropertiesWindow::GroupTitle(const QtPropertyReport::Group &group, bool comments) const {
    if (comments && group.scope == Scope::Selection)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Comment"));
    QString title;
    switch (group.scope) {
    case Scope::Selection:
        title = report_.items.size() > 1
                    ? text(QT_TRANSLATE_NOOP("PropertiesWindow", "Source totals (may be partial)"))
                    : text(QT_TRANSLATE_NOOP("PropertiesWindow", "Technical information"));
        break;
    case Scope::Folder:
        title = text(QT_TRANSLATE_NOOP("PropertiesWindow", "Current folder"));
        break;
    case Scope::Archive:
        title = text(QT_TRANSLATE_NOOP("PropertiesWindow", "Archive %1")).arg(group.level + 1);
        break;
    case Scope::ArchiveLink:
        title = text(QT_TRANSLATE_NOOP("PropertiesWindow", "Parent archive entry %1"))
                    .arg(group.level + 1);
        break;
    case Scope::UnopenedArchive:
        title = text(QT_TRANSLATE_NOOP("PropertiesWindow", "Unopened archive"));
        break;
    }
    return comments ? title + " · " + text(QT_TRANSLATE_NOOP("PropertiesWindow", "Comment"))
                    : title;
}
QString QtPropertiesWindow::Caption(unsigned row, Scope scope) const {
    const auto native = report_.nativeValues.constFind(row);
    if (scope == Scope::Folder && native != report_.nativeValues.cend() && native->id == kpidPath)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Location"));
    const auto id = report_.PropertyID(row);
    if (id == kpidCRC || id == kpidChecksum)
        return ToQString(report_.Strings[row]) + " " +
               text(QT_TRANSLATE_NOOP("PropertiesWindow", "(stored metadata)"));
    if (scope == Scope::Selection && id == kpidPackSize)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Reported packed size (may be partial)"));
    if (scope == Scope::Selection && report_.items.size() > 1 && id == kpidSize)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Reported size (may be partial)"));
    return ToQString(report_.Strings[row]);
}
QString QtPropertiesWindow::Value(unsigned row) const {
    const auto source = report_.nativeValues.constFind(row);
    if (source != report_.nativeValues.cend() &&
        source->availability == QtPropertyReport::Availability::Unknown)
        return available({});
    return ToQString(report_.Values[row]);
}
void QtPropertiesWindow::Refresh() {
    for (unsigned i = 0; i < report_.Strings.Size(); ++i)
        report_.RefreshRow(i);
    summary_->SetSource(SummaryName(), SummaryType());
    location_->setText(SummaryLocation());
    size_->setText(SummarySize());
    modified_->setText(SummaryModified());
    if (selectedNames_) {
        QStringList names;
        for (const auto &item : report_.items)
            names.append(item.prefix + available(item.name));
        selectedNames_->setText(names.join('\n'));
    }
    for (const auto &row : rows_) {
        const QString caption = Caption(row.source, row.scope);
        row.controls.caption->setText(caption);
        row.controls.value->setAccessibleName(caption);
        row.controls.value->setText(Value(row.source));
    }
    for (const auto &section : sections_)
        section.control->button()->setText(
            GroupTitle(report_.groups[section.group], section.comments));
}
QString QtPropertiesWindow::Information() const {
    QStringList result{
        SummaryName(), SummaryType(),
        text(QT_TRANSLATE_NOOP("PropertiesWindow", "Location")) + ": " + SummaryLocation(),
        text(QT_TRANSLATE_NOOP("PropertiesWindow", "Size")) + ": " + SummarySize(),
        text(QT_TRANSLATE_NOOP("PropertiesWindow", "Modified")) + ": " + SummaryModified()};
    if (report_.items.size() > 1)
        for (const auto &item : report_.items)
            result.append(item.prefix + available(item.name));
    for (const auto &group : report_.groups) {
        if (group.rows.isEmpty())
            continue;
        result.append("\n" + GroupTitle(group, false));
        for (const auto row : group.rows)
            if (report_.selectionSummaryRow != row)
                result.append(Caption(row, group.scope) + ": " + Value(row));
    }
    return result.join('\n') + '\n';
}
void QtPropertiesWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && summary_)
        Refresh();
    QDialog::changeEvent(event);
}
void QtPropertiesWindow::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!firstShow_)
        return;
    firstShow_ = false;
    if (size() != initialFrame_)
        return;
    const auto initial = size();
    QTimer::singleShot(0, this, [this, initial] {
        if (size() == initial)
            QtFitScrollableDialog(*this);
    });
}
void QtPropertiesWindow::done(int result) {
    const bool restoreFocus = isActiveWindow();
    const auto focus = returnFocus_;
    QDialog::done(result);
    if (restoreFocus && focus && focus->isVisible()) {
        focus->window()->activateWindow();
        focus->setFocus(Qt::OtherFocusReason);
    }
}
