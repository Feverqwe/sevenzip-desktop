#include "filemanager/properties/qtpropertieswindow.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariantConv.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/properties/qtpropertyformat.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/locale/qtregionalsettings.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/formatting/qtregionalformat.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kInitialHeight = 440;
constexpr int kCaptionWidth = 130;
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
    case Scope::Checksums:
        return "propertiesChecksums";
    }
    return {};
}
} // namespace

QtPropertiesWindow::QtPropertiesWindow(QtPropertyReport report, QWidget *parent,
                                       std::function<QWidget *()> returnFocus)
    : QDialog(parent, Qt::Window), report_(std::move(report)),
      returnFocus_(std::move(returnFocus)) {
    setObjectName("selectedPropertiesDialog");
    setWindowModality(Qt::NonModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindLangText(this, IDS_PROPERTIES);
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::NoButton, this);
    buttons->setObjectName("propertiesActions");
    auto *copy = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    copy->setObjectName("propertiesCopyInformation");
    copy->setAutoDefault(false);
    QtBindLocalText(copy, "PropertiesWindow",
                    QT_TRANSLATE_NOOP("PropertiesWindow", "Copy information"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(Information()); });
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    const bool directory =
        report_.items.isEmpty() ||
        (report_.items.size() == 1 && report_.items.first().directory.value_or(false));
    summary_ = new QtSourceSummary(
        style()->standardIcon(directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon), {}, {}, this);
    summary_->setObjectName("propertiesIdentity");
    summary_->SetSelectable(true);
    layout->addWidget(summary_);
    auto *primary = new QtMetadataRows(this, kCaptionWidth);
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
        section->setExpanded(true);
        section->setObjectName("propertiesSelectedObjects");
        QtBindLocalText(section->button(), "PropertiesWindow",
                        QT_TRANSLATE_NOOP("PropertiesWindow", "Selected objects"));
        layout->addWidget(section);
    }
    for (qsizetype i = 0; i < report_.groups.size(); ++i) {
        BuildGroup(i, false, *layout);
        BuildGroup(i, true, *layout);
    }
    Refresh();
    resize(520, kInitialHeight);
    QtFitScrollableDialog(*this, kInitialHeight);
    initialFrame_ = size();
    QtRegionalSettings::Observe(*this, [this] { Refresh(); });
}

void QtPropertiesWindow::BuildGroup(qsizetype index, bool comments, QVBoxLayout &layout) {
    const auto &group = report_.groups[index];
    auto *values = new QtMetadataRows(this, kCaptionWidth);
    values->setObjectName(scopeName(group) + (comments ? "Comments" : ""));
    int count = 0;
    for (const auto row : group.rows) {
        if (report_.selectionSummaryRow == row)
            continue; // SourceSummary already owns the selected-object count.
        if ((report_.PropertyID(row) == kpidComment) != comments)
            continue;
        const auto controls = values->AddLiteralRow(Caption(row, group.scope, true), Value(row));
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
    section->setExpanded(true);
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
    const auto notCalculated = QT_TRANSLATE_NOOP("PropertiesWindow", "Not calculated");
    if (report_.items.size() > 1)
        return text(notCalculated);
    const auto scope = report_.items.isEmpty() ? Scope::Folder : Scope::Selection;
    for (const auto &group : report_.groups)
        if (group.scope == scope) {
            const QString value = PropertyValue(group, kpidSize);
            if (!value.isEmpty())
                return value;
        }
    const bool folder = report_.items.isEmpty() || report_.items.first().directory.value_or(false);
    return folder ? text(notCalculated) : QtRegionalFormat::bytes(std::nullopt);
}
QString QtPropertiesWindow::SummaryModified() const {
    if (report_.items.size() > 1)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Not applicable"));
    const auto scope = report_.items.isEmpty() ? Scope::Folder : Scope::Selection;
    for (const auto &group : report_.groups)
        if (group.scope == scope) {
            const QString value = PropertyValue(group, kpidMTime);
            if (!value.isEmpty())
                return value;
        }
    return QString::fromUtf8("—");
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
    case Scope::Checksums:
        title = text(QT_TRANSLATE_NOOP("PropertiesWindow", "Checksums"));
        break;
    }
    return comments ? title + " · " + text(QT_TRANSLATE_NOOP("PropertiesWindow", "Comment"))
                    : title;
}
QString QtPropertiesWindow::Caption(unsigned row, Scope scope, bool compact) const {
    const auto native = report_.nativeValues.constFind(row);
    if (scope == Scope::Folder && native != report_.nativeValues.cend() && native->id == kpidPath)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Location"));
    const auto id = report_.PropertyID(row);
    if (compact && id == kpidChangeTime)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Metadata changed"));
    if (compact && scope == Scope::Selection && id == kpidPackSize)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Packed size"));
    if (compact && scope == Scope::Selection && id == kpidSize)
        return text(QT_TRANSLATE_NOOP("PropertiesWindow", "Size"));
    if (compact)
        return ToQString(report_.Strings[row]);
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
    if (source != report_.nativeValues.cend() && IsByteSizeProp(source->id)) {
        if (const auto value = QtUnsignedPropertyValue(source->value))
            return QtRegionalFormat::exactBytes(*value);
    }
    if (source != report_.nativeValues.cend() && source->value.vt == VT_FILETIME)
        return QtFormatPropertyTime(source->value).detail;
    return ToQString(report_.Values[row]);
}
QString QtPropertiesWindow::PropertyValue(const QtPropertyReport::Group &group, UInt32 id,
                                          bool detail) const {
    for (const auto row : group.rows) {
        const auto source = report_.nativeValues.constFind(row);
        if (source == report_.nativeValues.cend() || source->id != id)
            continue;
        if (source->availability != QtPropertyReport::Availability::Known)
            break;
        if (detail)
            return Value(row);
        if (id == kpidMTime)
            return QtFormatPropertyTime(source->value).display;
        if (const auto value = QtUnsignedPropertyValue(source->value))
            return QtRegionalFormat::bytes(*value);
    }
    return {};
}
void QtPropertiesWindow::Refresh() {
    for (unsigned i = 0; i < report_.Strings.Size(); ++i)
        report_.RefreshRow(i);
    QString path = report_.location;
    if (!path.isEmpty() && report_.items.size() == 1)
        path = QDir(path).filePath(report_.items.first().prefix + report_.items.first().name);
    summary_->SetSource(SummaryName(), path);
    summary_->SetDescription(SummaryType());
    location_->setText(SummaryLocation());
    size_->setText(SummarySize());
    modified_->setText(SummaryModified());
    size_->setToolTip({});
    modified_->setToolTip({});
    const auto primaryScope = report_.items.isEmpty() ? Scope::Folder : Scope::Selection;
    for (const auto &group : report_.groups)
        if (group.scope == primaryScope && report_.items.size() <= 1) {
            size_->setToolTip(PropertyValue(group, kpidSize, true));
            modified_->setToolTip(PropertyValue(group, kpidMTime, true));
        }
    if (selectedNames_) {
        QStringList names;
        for (const auto &item : report_.items)
            names.append(item.prefix + available(item.name));
        selectedNames_->setText(names.join('\n'));
    }
    for (const auto &row : rows_) {
        const QString caption = Caption(row.source, row.scope, true);
        const QString fullCaption = Caption(row.source, row.scope);
        row.controls.caption->setText(caption);
        row.controls.caption->setToolTip(caption == fullCaption ? QString() : fullCaption);
        row.controls.value->setAccessibleName(fullCaption);
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
    if ((event->type() == QEvent::LanguageChange || event->type() == QEvent::LocaleChange) &&
        summary_)
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
            QtFitScrollableDialog(*this, kInitialHeight);
    });
}
void QtPropertiesWindow::done(int result) {
    const bool restoreFocus = isActiveWindow();
    const auto returnFocus = returnFocus_;
    QDialog::done(result);
    if (!restoreFocus || !returnFocus)
        return;
    const QPointer<QWidget> focus = returnFocus();
    if (focus && focus->isVisible() && focus->isEnabled()) {
        focus->window()->activateWindow();
        if (focus)
            focus->setFocus(Qt::OtherFocusReason);
    }
}
