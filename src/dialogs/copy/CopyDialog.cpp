// Adapted from 7-Zip CPP/7zip/UI/FileManager/CopyDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt layouts replace Win32 OnSize/control attachment. Folder browsing uses
// the native Qt dialog; the upstream normalization and selection reset remain.
#include "dialogs/copy/CopyDialog.h"
#include "7zip/UI/FileManager/CopyDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileName.h"
#include "dialogs/browse/BrowseDialog.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogunits.h"

#include "localization/qtlangbuttonbox.h"
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

CCopyDialog::CCopyDialog(const QString &title, const QString &label, const QString &path,
                         const QString &info, QWidget *parent)
    : QDialog(parent), Title(ToUString(title)), Static(ToUString(label)), Value(ToUString(path)),
      Info(ToUString(info)) {
    OnInit();
}

CCopyDialog::CCopyDialog(bool move, const QString &initialPath, const QStringList &sources,
                         const QStringList &history, QWidget *parent, const QString &info)
    : QDialog(parent), Title(ToUString(QtLangText(move ? IDS_MOVE : IDS_COPY))),
      Static(ToUString(QtLangText(move ? IDS_MOVE_TO : IDS_COPY_TO))),
      Value(ToUString(initialPath)), Info(ToUString(info)) {
    for (const QString &s : history)
        Strings.Add(ToUString(s));
    // The Qt caller supplies filenames, rather than IFolder property indices.
    if (info.isNull())
        for (const QString &source : sources) {
            if (!Info.IsEmpty())
                Info.Add_LF();
            Info += ToUString(QFileInfo(source).fileName());
        }
    OnInit();
    BindLanguage(move ? IDS_MOVE : IDS_COPY, move ? IDS_MOVE_TO : IDS_COPY_TO);
}

void CCopyDialog::BindLanguage(UInt32 titleID, UInt32 promptID, const QString &titleSuffix) {
    setProperty("languageSuffix", titleSuffix);
    QtBindLangText(this, titleID, ToQString(Title));
    QtBindLangText(findChild<QLabel *>("copyDestinationLabel"), promptID, ToQString(Static));
    QtBindLangText(_path, promptID, ToQString(Static), true);
    auto *buttons = static_cast<QtLangButtonBox *>(findChild<QDialogButtonBox *>("copyButtons"));
    if (titleID == IDS_COPY)
        buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Copy"));
    else if (titleID == IDS_MOVE)
        buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Move"));
    else if (titleID == IDS_COMBINE)
        buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Combine"));
}

bool CCopyDialog::OnInit() {
    QtUseContextDialogModality(*this);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setObjectName("copyDialog");
    setWindowTitle(ToQString(Title));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("copyButtons");
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    auto *label = new QtWrappingLabel(ToQString(Static), this);
    label->setWordWrap(true);
    label->setObjectName("copyDestinationLabel");
    layout->addWidget(label);
    auto *pathRow = new QHBoxLayout;
    _path = new QComboBox(this);
    _path->setObjectName("copyDestination");
    _path->setAccessibleName(ToQString(Static));
    _path->setEditable(true);
    _path->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    _path->setInsertPolicy(QComboBox::NoInsert);
    FOR_VECTOR(i, Strings)
    _path->addItem(ToQString(Strings[i]));
    _path->setCurrentText(ToQString(Value));
    label->setBuddy(_path);
    _path->setMinimumContentsLength(12);
    _path->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pathRow->addWidget(_path, 1);
    auto *browse = new QPushButton("…", this);
    browse->setObjectName("copyBrowseButton");
    browse->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    QtBindLangText(browse, IDS_SET_FOLDER, "Select folder", true);
    browse->setFixedWidth(QtDialogUnits(font()).pixels(QSize(20, 0)).width());
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);
    // Native SS_LEFTNOWORDWRAP shows a bounded summary. Qt scrolling keeps long
    // Unicode filenames and large selections reachable at compact sizes.
    auto *info = new QPlainTextEdit(ToQString(Info), this);
    info->setObjectName("copySelectionInfo");
    QtBindLangText(info, IDS_PROPERTIES, "Properties", true);
    info->setReadOnly(true);
    info->document()->setDocumentMargin(0);
    // Native IDT_COPY_INFO is static, not a separate keyboard tab stop.
    info->setFocusPolicy(Qt::NoFocus);
    info->setFrameShape(QFrame::NoFrame);
    info->setLineWrapMode(QPlainTextEdit::NoWrap);
    info->setMinimumHeight(fontMetrics().lineSpacing() * 3);
    layout->addWidget(info, 1);
    connect(buttons, &QDialogButtonBox::accepted, this, &CCopyDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &CCopyDialog::OnButtonSetPath);
    resize(560, minimumSizeHint().height() + fontMetrics().lineSpacing() * 2);
    QtNormalizeDialogSize(*this, true);
    _path->setFocus();
    _path->lineEdit()->selectAll();
    return true;
}

void CCopyDialog::OnButtonSetPath() {
    UString currentPath;
    if (!ReadPath(currentPath))
        return;
    UString selectedPath;
    if (!MyBrowseForFolder(this, ToUString(QtLangText(IDS_SET_FOLDER)), currentPath, selectedPath))
        return;
    NWindows::NFile::NName::NormalizeDirPathPrefix(selectedPath);
    _path->setCurrentIndex(-1);
    _path->setCurrentText(ToQString(selectedPath));
}

bool CCopyDialog::ReadPath(UString &path) {
    const QString text = _path->currentText();
    if (text.contains(QChar::Null)) {
        QMessageBox::critical(this, windowTitle(),
                              ToQString(NWindows::NError::MyFormatMessage(E_INVALIDARG)));
        _path->setFocus();
        return false;
    }
    path = ToUString(text);
    return true;
}

void CCopyDialog::OnOK() {
    if (!ReadPath(Value))
        return;
    QDialog::accept();
}

void CCopyDialog::accept() { OnOK(); }

QString CCopyDialog::destination() const {
    return result() == QDialog::Accepted ? ToQString(Value) : _path->currentText();
}
