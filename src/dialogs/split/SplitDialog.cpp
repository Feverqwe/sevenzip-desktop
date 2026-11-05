// Adapted from 7-Zip CPP/7zip/UI/FileManager/SplitDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt layouts/native folder browsing replace Win32 controls/OnSize; functional
// OnInit/OnOK/OnButtonSetPath retain upstream flow and public result members.
#include "dialogs/split/SplitDialog.h"
#include "7zip/UI/FileManager/CopyDialogRes.h"
#include "7zip/UI/FileManager/SplitDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileName.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/SplitUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CSplitDialog::CSplitDialog(const QString &filePath, const QString &path, QWidget *parent)
    : QDialog(parent), FilePath(filePath), Path(path) {
    OnInit();
}

bool CSplitDialog::OnInit() {
    QtUseContextDialogModality(*this);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setObjectName("splitDialog");
    if (!FilePath.isEmpty())
        setProperty("languageSuffix", ' ' + FilePath);
    QtBindResourceText(this, IDD_SPLIT, "Split File");
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("splitButtons");
    buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Split"));
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    auto *pathLabel = new QtWrappingLabel(this);
    pathLabel->setWordWrap(true);
    QtBindResourceText(pathLabel, IDT_SPLIT_PATH, "&Split to:");
    pathLabel->setObjectName("splitPathLabel");
    layout->addWidget(pathLabel);
    auto *row = new QHBoxLayout;
    _pathCombo = new QComboBox(this);
    _pathCombo->setObjectName("volumeDestination");
    QtBindLangText(_pathCombo, IDT_SPLIT_PATH, "Split to:", true);
    _pathCombo->setEditable(true);
    _pathCombo->setInsertPolicy(QComboBox::NoInsert);
    _pathCombo->setMinimumContentsLength(12);
    _pathCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pathLabel->setBuddy(_pathCombo);
    row->addWidget(_pathCombo, 1);
    auto *browse = new QPushButton("...", this);
    browse->setObjectName("volumeBrowse");
    QtBindLangText(browse, IDT_SPLIT_PATH, "Split to:", true);
    browse->setFixedWidth(
        qMax(QtDialogUnits(font()).pixels(QSize(20, 0)).width(), browse->sizeHint().height()));
    row->addWidget(browse);
    layout->addLayout(row);
    layout->addSpacing(12);
    auto *volumeLabel = new QtWrappingLabel(this);
    volumeLabel->setWordWrap(true);
    QtBindResourceText(volumeLabel, IDT_SPLIT_VOLUME, "Split to &volumes,  bytes:");
    volumeLabel->setObjectName("splitVolumeLabel");
    layout->addWidget(volumeLabel);
    _volumeCombo = new QComboBox(this);
    _volumeCombo->setObjectName("volumeSize");
    QtBindLangText(_volumeCombo, IDT_SPLIT_VOLUME, "Split to volumes, bytes:", true);
    _volumeCombo->setEditable(true);
    _volumeCombo->setInsertPolicy(QComboBox::NoInsert);
    volumeLabel->setBuddy(_volumeCombo);
    _volumeCombo->setMinimumContentsLength(12);
    layout->addWidget(_volumeCombo);
    connect(buttons, &QDialogButtonBox::accepted, this, &CSplitDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &CSplitDialog::OnButtonSetPath);

    _pathCombo->setCurrentText(Path);
    AddVolumeItems(*_volumeCombo);
    _volumeCombo->setCurrentIndex(0);
    _pathCombo->setFocus();
    _pathCombo->lineEdit()->selectAll();
    resize(QSize(480, sizeHint().height()).expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
    return true;
}

void CSplitDialog::OnButtonSetPath() {
    QString currentPath;
    if (!ReadPath(currentPath))
        return;
    UString selectedPath;
    if (!MyBrowseForFolder(this, ToUString(QtLangText(IDS_SET_FOLDER)), ToUString(currentPath),
                           selectedPath))
        return;
    NWindows::NFile::NName::NormalizeDirPathPrefix(selectedPath);
    _pathCombo->setCurrentIndex(-1);
    _pathCombo->setCurrentText(ToQString(selectedPath));
}

bool CSplitDialog::ReadPath(QString &path) {
    const QString text = _pathCombo->currentText();
    if (text.contains(QChar::Null)) {
        QMessageBox::critical(this, windowTitle(),
                              ToQString(NWindows::NError::MyFormatMessage(E_INVALIDARG)));
        _pathCombo->setFocus();
        return false;
    }
    path = text;
    return true;
}

void CSplitDialog::OnOK() {
    if (!ReadPath(Path))
        return;
    if (!ParseVolumeSizesText(_volumeCombo->currentText(), VolumeSizes) || VolumeSizes.IsEmpty()) {
        QMessageBox box(QMessageBox::Critical, "7-Zip", QtLangText(IDS_INCORRECT_VOLUME_SIZE),
                        QMessageBox::Ok, this);
        box.setObjectName("splitVolumeError");
        box.setTextFormat(Qt::PlainText);
        box.exec();
        return;
    }
    QDialog::accept();
}

void CSplitDialog::accept() { OnOK(); }
