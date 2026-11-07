#include "platform/associations/qtassociatedfileservices.h"
#include "localization/qtlanguageservices.h"
#include "platform/associations/qtfileassociationprobe.h"
#include "settings/ViewSettings.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#ifdef Q_OS_WIN
#include "Windows/ProcessUtils.h"
#include "platform/associations/qtassociatedfilelaunch_win.h"
#endif

namespace {
bool OpenAssociation(const QUrl &url) { return QDesktopServices::openUrl(url); }
} // namespace

QtAssociatedFileServices::QtAssociatedFileServices(
    std::function<bool(const QUrl &)> opener,
    std::function<bool(CPanel &, const QtArchiveUpdateBody &)> updateRunner,
    std::function<bool()> updateReady, std::function<bool(const QString &)> hasAssociation,
    QtKnownAssociationOpener knownOpener)
    : _opener(std::move(opener)), _knownOpener(std::move(knownOpener)),
      _updateRunner(std::move(updateRunner)), _updateReady(std::move(updateReady)),
      _hasAssociation(std::move(hasAssociation)) {}

bool QtAssociatedFileServices::ConfirmArchiveFallback(const UString &path, QWidget *window) {
    const QString file = ToQString(path);
    if ((_hasAssociation ? _hasAssociation : QtFileHasOpenAssociation)(file))
        return true;
    QMessageBox box(
        QMessageBox::Question,
        QCoreApplication::translate("qtassociatedfileservices", "Open in external application?"),
        QCoreApplication::translate(
            "qtassociatedfileservices",
            "This file could not be opened as an archive, and no application is registered for "
            "its file type. Try opening it in an external application?"),
        QMessageBox::Open | QMessageBox::Cancel, window);
    box.setObjectName("unknownFileOpenPrompt");
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(QFileInfo(file).fileName());
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Open;
}

bool QtAssociatedFileServices::CanHandleExternalUpdate() const {
    return !_updateReady || _updateReady();
}
bool QtAssociatedFileServices::RunArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) {
    return _updateRunner ? _updateRunner(panel, body)
                         : IQtExternalFileServices::RunArchiveUpdate(panel, body);
}

HRESULT QtAssociatedFileServices::StartApplicationKnown(const UString &dir, const UString &path,
                                                        QWidget *window,
                                                        IQtProcessHandle *&process) {
    process = nullptr;
    if (_knownOpener)
        return _knownOpener(path, process);
#ifdef Q_OS_WIN
    if (!_opener) {
        return _processServices.StartNative(
            [&](NWindows::CProcess &native) {
                return ::StartApplication(dir, path, window, native);
            },
            process);
    }
#endif
    return StartApplication(dir, path, window);
}

HRESULT QtAssociatedFileServices::StartApplication(const UString &dir, const UString &path,
                                                   QWidget *window) {
#ifdef Q_OS_WIN
    if (!_opener) {
        NWindows::CProcess native;
        return ::StartApplication(dir, path, window, native);
    }
#else
    (void)dir;
#endif
    // POSIX associations and explicitly injected openers have no waitable handle.
    bool opened = false;
    try {
        opened = (_opener ? _opener : OpenAssociation)(QUrl::fromLocalFile(ToQString(path)));
    } catch (...) {
        opened = false;
    }
    if (opened)
        return S_OK;
    QMessageBox box(
        QMessageBox::Warning, QCoreApplication::translate("qtassociatedfileservices", "Open file"),
        QCoreApplication::translate("qtassociatedfileservices", "Could not open file: %1")
            .arg(ToQString(path)),
        QMessageBox::Ok, window);
    box.setObjectName("externalFileOpenError");
    box.setTextFormat(Qt::PlainText);
    box.exec();
    return E_FAIL;
}
