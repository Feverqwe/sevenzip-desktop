// Adapted from 7-Zip MemDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/mem/MemDialog.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringToInt.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/qtguimessages.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/memory/qtmemorylimitinfo.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QAbstractButton>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRadioButton>

void CMemDialog::EnableSpin(bool enable) {
    _controls.value(IDC_MEM_SPIN)->setEnabled(enable);
    _controls.value(IDE_MEM_SPIN_EDIT)->setEnabled(enable);
}

void CMemDialog::SetItemText(unsigned id, const UString &text) {
    const auto value = QString::fromWCharArray(text);
    if (auto *label = qobject_cast<QLabel *>(_controls.value(id)))
        label->setText(value);
    else
        static_cast<QLineEdit *>(_controls.value(id))->setText(value);
}

void CMemDialog::GetItemText(unsigned id, UString &text) const {
    text = static_cast<QLineEdit *>(_controls.value(id))->text().toStdWString().c_str();
}

bool CMemDialog::IsButtonCheckedBool(unsigned id) const {
    return static_cast<QAbstractButton *>(_controls.value(id))->isChecked();
}

void CMemDialog::StepSpin(bool up) {
    UString text;
    GetItemText(IDE_MEM_SPIN_EDIT, text);
    const wchar_t *end;
    UInt32 value = ConvertStringToUInt32(text, &end);
    if (*end || text.IsEmpty())
        value = _spinMin;
    else if (up && value < _spinMax)
        ++value;
    else if (!up && value > _spinMin)
        --value;
    value = qBound(_spinMin, value, _spinMax);
    text.Empty();
    text.Add_UInt32(value);
    SetItemText(IDE_MEM_SPIN_EDIT, text);
}

bool CMemDialog::eventFilter(QObject *watched, QEvent *event) {
    auto *edit = _controls.value(IDE_MEM_SPIN_EDIT);
    if (watched == edit && edit->isEnabled() && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            // Original GuiCommon.rc UDS_ARROWKEYS routes the buddy edit's
            // arrows through the same native up/down control as its buttons.
            StepSpin(key->key() == Qt::Key_Up);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

INT_PTR CMemDialog::Create(QWidget *parent) {
    if (parent && parent != parentWidget())
        setParent(parent, windowFlags());
    OnInit();
    return exec();
}

void CMemDialog::reject() { done(IDCANCEL); }

static void AddSize_GB(UString &s, UInt32 size_GB, UInt32 id) {
    s.Add_LF();
    s += "    ";
    s += UString(QCoreApplication::translate("MemoryUnits", "%1 GB : %2")
                     .arg(QLocale().toString(size_GB), QString::fromWCharArray(LangString(id)))
                     .toStdWString()
                     .c_str());
}

void CMemDialog::AddInfoMessage_To_String(UString &s, const UInt32 *ramSize_GB) {
    AddLangString(s, IDS_MEM_REQUIRES_BIG_MEM);
    AddSize_GB(s, Required_GB, IDS_MEM_REQUIRED_MEM_SIZE);
    AddSize_GB(s, Limit_GB, IDS_MEM_CURRENT_MEM_LIMIT);
    if (ramSize_GB)
        AddSize_GB(s, *ramSize_GB, IDS_MEM_RAM_SIZE);
    if (!FilePath.IsEmpty()) {
        s.Add_LF();
        s += UString(QCoreApplication::translate("MemDialog", "File: %1")
                         .arg(QString::fromWCharArray(FilePath))
                         .toStdWString()
                         .c_str());
    }
}

bool CMemDialog::OnInit() {
    QtUseContextDialogModality(*this);
    if (_controls.isEmpty())
        BuildPresentation();
    const auto memory = QtQueryMemoryLimitInfo();
    const bool is_Allowed =
        (!memory.RamSizeDefined || memory.RamSize > ((UInt64)Required_GB << 30));
    _messageRam_GB = is_Allowed ? 0 : memory.RamSizeGB;
    ReloadInfoMessage(is_Allowed ? NULL : &memory.RamSizeGB);
    QtBindMemoryLimitUnitText(_controls.value(IDT_MEM_GB), memory.RamSizeDefined, memory.RamSizeGB);
    _spinMin = 1;
    _spinMax = memory.MaximumGB;
    {
        UString s;
        s.Add_UInt32(Required_GB);
        SetItemText(IDE_MEM_SPIN_EDIT, s);
    }
    EnableSpin(false);
    const UINT buttonId = is_Allowed ? IDR_MEM_ACTION_ALLOW : IDR_MEM_ACTION_SKIP_ARC;
    static_cast<QRadioButton *>(_controls.value(buttonId))->setChecked(true);
    _controls.value(IDX_MEM_REMEMBER)->setVisible(ShowRemember);
    adjustSize();
    _controls.value(IDX_MEM_SAVE_LIMIT)->setFocus();
    return true;
}

void CMemDialog::ReloadInfoMessage(const UInt32 *ramSize_GB) {
    UString s;
    if (ramSize_GB) {
        AddLangString(s, IDS_MEM_ERROR);
        s.Add_LF();
    }
    AddInfoMessage_To_String(s, ramSize_GB);
    if (!ArcPath.IsEmpty()) {
        s.Add_LF();
        AddLangString(s, TestMode ? IDS_PROGRESS_TESTING : IDS_PROGRESS_EXTRACTING);
        s += ": ";
        s += ArcPath;
    }
    SetItemText(IDT_MEM_MESSAGE, s);
}

void CMemDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && !_controls.isEmpty())
        ReloadInfoMessage(_messageRam_GB ? &_messageRam_GB : nullptr);
    QDialog::changeEvent(event);
}

void CMemDialog::OnContinue() {
    Remember = IsButtonCheckedBool(IDX_MEM_REMEMBER);
    NeedSave = IsButtonCheckedBool(IDX_MEM_SAVE_LIMIT);
    SkipArc = IsButtonCheckedBool(IDR_MEM_ACTION_SKIP_ARC);
    if (NeedSave) {
        UString s;
        GetItemText(IDE_MEM_SPIN_EDIT, s);
        const wchar_t *end;
        const UInt32 val = ConvertStringToUInt32(s.Ptr(), &end);
        if (s.IsEmpty() || *end != 0 || val > (1u << 30)) {
            ShowErrorMessageQt(this, NWindows::NError::MyFormatMessage(E_INVALIDARG));
            return;
        }
        Limit_GB = val;
    }
    done(IDCONTINUE);
}
