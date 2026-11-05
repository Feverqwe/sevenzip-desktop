#include "filemanager/window/qtnativeplaces.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSDrives.h"
#include "filemanager/providers/NetFolder.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/window/qtplaceprojection.h"
#include "localization/LangUtils.h"
#include <QApplication>

QtNativePlaces::QtNativePlaces(QObject *parent) : QAbstractListModel(parent) { Refresh(); }

QString QtNativePlaces::RootIdentity(const CPanel &panel) {
    if (const auto *network = dynamic_cast<const CNetFolder *>(panel.GetFolder());
        network && network->IsRootLocation())
        return QStringLiteral("network");
    if (dynamic_cast<const CFSDrives *>(panel.GetFolder())) {
#ifdef _WIN32
        if (panel.IsSuperDrivesPrefix())
            return {};
        return panel.IsDeviceDrivesPrefix() ? QStringLiteral("volumes")
                                            : QStringLiteral("computer");
#else
        return QStringLiteral("volumes");
#endif
    }
    return {};
}

int QtNativePlaces::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : places_.size();
}

QVariant QtNativePlaces::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.row() >= places_.size())
        return {};
    const auto &place = places_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return place.label;
    case Qt::ToolTipRole:
    case QtPlaceRoles::Path:
        return place.path;
    case QtPlaceRoles::Identity:
        return place.id;
    case Qt::DecorationRole:
        return QtCommandSymbol(place.icon);
    default:
        return {};
    }
}

void QtNativePlaces::Refresh() {
    const QString language =
        ToQString(g_LangID) + ':' + QCoreApplication::translate("QtWindowWorkspace", "Locations");
    if (language_ == language)
        return;
    QList<Place> places;
    CRootFolder root;
    root.Init();
    IFolderFolder &source = root;
    UInt32 count = 0;
    if (source.GetNumberOfItems(&count) != S_OK)
        return;
    int iconIndex = 0;
    for (UInt32 i = 0; i < count; ++i) {
        NWindows::NCOM::CPropVariant name;
        if (source.GetProperty(i, kpidName, &name) != S_OK || name.vt != VT_BSTR)
            continue;
        const UString nativeName(name.bstrVal);
        if (nativeName == RootFolder_GetName_Documents(iconIndex))
            continue;
        CMyComPtr<IFolderFolder> folder;
        if (source.BindToFolder(i, &folder) != S_OK || !folder)
            continue;
        QString label = ToQString(nativeName);
        if (label == QStringLiteral("\\\\."))
            label = QCoreApplication::translate("RootFolder", "Volumes");
        const bool computer = nativeName == RootFolder_GetName_Computer(iconIndex);
        const bool network = nativeName == RootFolder_GetName_Network(iconIndex);
        places.append({computer  ? QStringLiteral("computer")
                       : network ? QStringLiteral("network")
                                 : QStringLiteral("volumes"),
                       label, ToQString(GetFolderPath(folder)),
                       computer  ? QtCommandIcon::Computer
                       : network ? QtCommandIcon::Network
                                 : QtCommandIcon::Volumes});
    }
    bool sameIdentities = places.size() == places_.size();
    for (int i = 0; sameIdentities && i < places.size(); ++i)
        sameIdentities = places[i].id == places_[i].id;
    if (!sameIdentities)
        beginResetModel();
    places_ = std::move(places);
    language_ = language;
    if (!sameIdentities)
        endResetModel();
    else if (!places_.isEmpty())
        emit dataChanged(index(0, 0), index(places_.size() - 1, 0));
}
