#include "Windows/FileName.h"
#include "dialogs/copy/CopyDialog.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <utility>

namespace {
QString Text(const char *source) { return QCoreApplication::translate("CopyRedesign", source); }

UString DirectoryPrefix(const QString &path) {
    UString prefix = ToUString(QDir::fromNativeSeparators(path));
    NWindows::NFile::NName::NormalizeDirPathPrefix(prefix);
    return prefix;
}

bool SameDirectory(const QString &first, const QString &second) {
    return !first.isEmpty() && !second.isEmpty() &&
           CompareFileNames(DirectoryPrefix(QDir::cleanPath(first)),
                            DirectoryPrefix(QDir::cleanPath(second))) == 0;
}

bool IsName(const QString &name) {
    if (name.isEmpty() || name == "." || name == ".." || name.contains(QChar::Null) ||
        name.contains('/'))
        return false;
#ifdef Q_OS_WIN
    if (name.contains('\\'))
        return false;
#endif
    return true;
}

bool HasBlockedDestinationDirectory(const QFileInfo &destination, bool needsDirectory) {
    QString path = needsDirectory ? destination.absoluteFilePath() : destination.absolutePath();
    for (;;) {
        const QFileInfo directory(path);
        if (directory.exists() || directory.isSymLink())
            return !directory.isDir();
        const QString parent = directory.absolutePath();
        if (parent == path)
            return false;
        path = parent;
    }
}
} // namespace

void CCopyDialog::BuildDestinationOptions(QVBoxLayout &layout) {
    _peer = new QPushButton(this);
    _peer->setObjectName("copyPeerDestination");
    _peer->setAutoDefault(false);
    QtBindLocalText(_peer, "CopyRedesign", QT_TRANSLATE_NOOP("CopyRedesign", "Other panel"));
    _peer->hide();
    layout.addWidget(_peer, 0, Qt::AlignRight);
    connect(_peer, &QPushButton::clicked, this, [this] {
        if (!_destinationContext || !_destinationContext->PeerWritable)
            return;
        SetPathText(_destinationContext->PeerDestination);
        _pathEdit->setFocus();
        _pathEdit->selectAll();
    });
    if (_sources.size() == 1) {
        _nameLabel = new QtWrappingLabel(this);
        _nameLabel->setWordWrap(true);
        _nameLabel->setObjectName("copyDestinationNameLabel");
        QtBindLocalText(_nameLabel, "CopyRedesign",
                        QT_TRANSLATE_NOOP("CopyRedesign", "Name at destination"));
        _name = new QLineEdit(this);
        _name->setObjectName("copyDestinationName");
        QtBindLocalText(_name, "CopyRedesign",
                        QT_TRANSLATE_NOOP("CopyRedesign", "Name at destination"), true);
        _nameLabel->setBuddy(_name);
        layout.addWidget(_nameLabel);
        layout.addWidget(_name);
        _nameLabel->hide();
        _name->hide();
        connect(_name, &QLineEdit::textChanged, this, &CCopyDialog::UpdateDestination);
    }
    _destinationMessage = new QtWrappingLabel(this);
    _destinationMessage->setObjectName("copyDestinationMessage");
    _destinationMessage->setTextFormat(Qt::PlainText);
    _destinationMessage->setWordWrap(true);
    _destinationMessage->hide();
    layout.addWidget(_destinationMessage);
}

void CCopyDialog::ConfigureDestination(QtCopyDestinationContext context) {
    _destinationContext = std::move(context);
    if (_name)
        _name->setText(_destinationContext->SourceName);
    if (_peer) {
        _peer->setVisible(!_destinationContext->PeerDestination.isNull());
        _peer->setEnabled(_destinationContext->PeerWritable);
        _peer->setToolTip(_destinationContext->PeerDestination);
    }
    UpdateDestination();
    ensurePolished();
    layout()->activate();
    int contentHeight = 0;
    int chromeHeight = 0;
    if (auto *scroll = findChild<QScrollArea *>("dialogContentScroll")) {
        if (scroll->widget()->layout())
            scroll->widget()->layout()->activate();
        contentHeight = scroll->widget()->heightForWidth(scroll->viewport()->width());
        chromeHeight = height() - scroll->height();
        scroll->updateGeometry();
    }
    layout()->invalidate();
    resize(width(), qMax(sizeHint().height(), contentHeight + chromeHeight));
    QtNormalizeDialogSize(*this, true);
}

QString CCopyDialog::DraftDestination() const {
    if (!_useName || !_name)
        return _pathEdit->text();
    return ToQString(DirectoryPrefix(_pathEdit->text()) + ToUString(_name->text()));
}

QString CCopyDialog::PreviewDestination(const QString &value) const {
    if (!_destinationContext || _destinationContext->RelativeBase.isNull() ||
        NWindows::NFile::NName::IsAbsolutePath(ToUString(value)))
        return value;
    // CApp adds its native panel prefix to a relative Value. This preview
    // never replaces that raw Value or the native normalization step.
    return _destinationContext->RelativeBase + value;
}

void CCopyDialog::UpdateDestination() {
    if (!_copyMove || !_pathEdit || !_destinationMessage)
        return;
    const QString path = _pathEdit->text();
    const QString preview = PreviewDestination(path);
    const QFileInfo destination(preview);
    const bool directory =
        !path.isEmpty() &&
        (path.endsWith('/') || path.endsWith(QDir::separator()) || destination.isDir());
    const bool peerDestination =
        _destinationContext && SameDirectory(preview, _destinationContext->PeerDestination);
    const bool nameAvailable = _destinationContext && _name && directory &&
                               _destinationContext->CanRename &&
                               _destinationContext->CanRename(path);
    if (_name) {
        _name->setVisible(nameAvailable);
        _nameLabel->setVisible(nameAvailable);
    }
    _useName = nameAvailable && _name->text() != _destinationContext->SourceName;
    QString error;
    if (_sources.isEmpty())
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "Select one or more items."));
    else if (path.isEmpty())
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "Choose a destination."));
    else if (path.contains(QChar::Null))
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "The destination is invalid."));
    else if (nameAvailable && !IsName(_name->text()))
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "Enter a file or folder name."));
    else if (destination.isDir() && !destination.isWritable())
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "The destination is read-only."));
    else if (peerDestination && !_destinationContext->PeerWritable)
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "The destination is read-only."));
    else if (!peerDestination &&
             HasBlockedDestinationDirectory(destination, directory || _sources.size() > 1))
        error = Text(QT_TRANSLATE_NOOP("CopyRedesign", "The destination is invalid."));
    else if (_destinationContext && !_useName &&
             SameDirectory(preview, _destinationContext->SourceDirectory))
        error = Text(
            QT_TRANSLATE_NOOP("CopyRedesign", "Choose another destination or change the name."));
    else if (_sources.size() == 1 &&
             CompareFileNames(ToUString(PreviewDestination(DraftDestination())),
                              ToUString(_sources.first())) == 0)
        error = Text(
            QT_TRANSLATE_NOOP("CopyRedesign", "Choose another destination or change the name."));
    _canAccept = error.isEmpty();
    _destinationMessage->setText(error);
    _destinationMessage->setVisible(!error.isEmpty());
    if (auto *buttons = findChild<QDialogButtonBox *>("copyButtons"))
        buttons->button(QDialogButtonBox::Ok)->setEnabled(_canAccept);
}
