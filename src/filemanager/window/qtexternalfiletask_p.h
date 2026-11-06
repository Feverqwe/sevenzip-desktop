#pragma once
#include "filemanager/external/qtexternalarchivetarget.h"
#include "filemanager/external/qtexternalfilerecord.h"
#include "filemanager/external/qtexternalfilesession.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "fileops/transfer/filetransferoperation.h"
#include "platform/process/qtexternalprocess.h"
#include <QCheckBox>
#include <QDialog>
#include <QEventLoopLocker>
#include <QLabel>
#include <QPointer>
#include <QPushButton>

struct QtExternalFileTask::State {
    CTempFileInfo file;
    std::optional<QtExternalArchiveTarget> target;
    CExitEventLauncher launcher;
    QtNativeProcessServices processes;
    std::unique_ptr<QEventLoopLocker> keepAlive;
    QPointer<QDialog> dialog;
    QPointer<QDialog> question;
    QPointer<FileTransferOperation> copy;
    QPointer<QLabel> status;
    QPointer<QLabel> retainedText;
    QPointer<QWidget> retainedRows;
    QPointer<QCheckBox> confirmed;
    QPointer<QPushButton> check;
    QPointer<QPushButton> save;
    QString prefix;
    QString directoryIdentity;
    const char *message = nullptr;
    QStringList retained;
    std::optional<QtExternalFileRecord> record;
    bool persisted = false;
    bool recovered = false;
    bool launched = false;
    bool known = false;
    bool updating = false;
    bool publishing = false;
    bool published = false;
    unsigned exports = 0;
    QtExternalObservation observation = QtExternalObservation::NotStarted;
};
