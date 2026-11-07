#include "Windows/ErrorMsg.h"
#include "archive/callbacks/qtguimessages.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

namespace {
struct Choice {
    QVariant value;
    bool selected;

    explicit Choice(const QComboBox &control)
        : value(control.currentData()), selected(control.currentIndex() >= 0) {}

    void restore(QComboBox &control) const {
        const int index = selected ? control.findData(value) : -1;
        Q_ASSERT(!selected || index >= 0);
        control.setCurrentIndex(index);
    }

    void restoreMemory(QComboBox &control) const {
        if (!selected || control.findData(value) >= 0) {
            restore(control);
            return;
        }
        // The source normalizes equivalent memory tokens when rebuilding its
        // choices. Keep the original token if that same limit is still offered.
        NCompression::CMemUse saved;
        saved.Parse(value.toString());
        if (saved.IsDefined)
            for (int i = 0; i < control.count(); ++i) {
                NCompression::CMemUse candidate;
                candidate.Parse(control.itemData(i).toString());
                if (candidate.IsDefined && candidate.IsPercent == saved.IsPercent &&
                    candidate.Val == saved.Val) {
                    control.setItemData(i, value);
                    control.setCurrentIndex(i);
                    return;
                }
            }
        restore(control);
    }
};
} // namespace

// The saved registry is a persistence representation, not the complete draft:
// it trims parameters and normalizes Auto and large dictionary selections.
// Retain the live choices separately while their existing owner edits them.
struct CreateArchiveDialog::ExpertDraft {
    NCompression::CInfo registry;
    NCompression::CInfo renderingRegistry;
    CCompressDialog naming;
    Choice level, method, dictionary, order, solid, threads, memory, update, paths;
    QString name, folder, parameters;
    bool shared;

    explicit ExpertDraft(const CreateArchiveDialog &owner)
        : registry(owner.m_RegistryInfo), naming(owner.archiveNameState_), level(*owner.level_),
          method(*owner.method_), dictionary(*owner.dictionary_), order(*owner.wordSize_),
          solid(*owner.solidBlock_), threads(*owner.threads_), memory(*owner.memUse_),
          update(*owner.updateMode_), paths(*owner.pathMode_), name(owner.name_->text()),
          folder(owner.archiveFolder_->text()), parameters(owner.parameters_->text()),
          shared(owner.sharedFiles_->isChecked()) {}
};

std::unique_ptr<CreateArchiveDialog::ExpertDraft> CreateArchiveDialog::captureExpertDraft() {
    auto draft = std::make_unique<ExpertDraft>(*this);
    SaveOptionsInMem();
    draft->renderingRegistry = m_RegistryInfo;
    return draft;
}

void CreateArchiveDialog::restoreExpertDraft(const ExpertDraft &draft) {
    const QSignalBlocker level(level_), method(method_), dictionary(dictionary_), order(wordSize_),
        solid(solidBlock_), threads(threads_), memory(memUse_), update(updateMode_),
        paths(pathMode_), shared(sharedFiles_), name(name_), folder(archiveFolder_),
        parameters(parameters_);
    m_RegistryInfo = draft.renderingRegistry;
    archiveNameState_ = draft.naming;
    archiveNameState_.m_Method = method_;
    sharedFiles_->setChecked(draft.shared);
    FormatChanged();
    draft.level.restore(*level_);
    draft.method.restore(*method_);
    MethodChanged();
    draft.dictionary.restore(*dictionary_);
    draft.order.restore(*wordSize_);
    SetSolidBlockSize2(UInt32(-1));
    draft.solid.restore(*solidBlock_);
    draft.memory.restoreMemory(*memUse_);
    SetNumThreads2();
    draft.threads.restore(*threads_);
    draft.update.restore(*updateMode_);
    draft.paths.restore(*pathMode_);
    SetMemoryUsage();
    // Intermediate source recalculation may change archive naming. Restore the
    // original naming state and raw text after all dependent controls settle.
    archiveNameState_ = draft.naming;
    archiveNameState_.m_Method = method_;
    name_->setText(draft.name);
    archiveFolder_->setText(draft.folder);
    parameters_->setText(draft.parameters);
    m_RegistryInfo = draft.registry;
    updateAdvancedSummary();
}

void CreateArchiveDialog::showExpertOptions(QWidget *focus) {
    auto draft = captureExpertDraft();
    bool applied = false;
    const auto rollback = qScopeGuard([&] {
        if (!applied)
            restoreExpertDraft(*draft);
    });
    QDialog editor(this);
    editor.setObjectName("archiveExpertDialog");
    QtUseContextDialogModality(editor);
    QtTaskPresentation::Watch(editor, QtOperationPhase::Draft);
    QtBindLocalText(&editor, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Expert options"));
    const auto returnContent = qScopeGuard([&] {
        expertContent_->setParent(this);
        expertContent_->hide();
    });
    // The source/profile outlive the temporary panel; neither is stored in the
    // reusable content widget. Cancel discards this entire metadata draft.
    CCompressDialog metadataSource;
    configureMetadataSource(metadataSource);
    const CCompressDialog initialMetadataSource = metadataSource;
    NCompression::CFormatOptions metadataProfile = Get_FormatOptions();
    metadataSource.FormatOptions = &metadataProfile;
    auto *metadata = new COptionsPanel(&metadataSource);
    auto metadataSection = std::make_unique<QtDisclosure>(metadata, expertContent_);
    metadataSection->setObjectName("archiveMetadataSection");
    QtBindLocalText(metadataSection->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Metadata and time"));
    metadata->initialize();
    static_cast<QVBoxLayout *>(expertContent_->layout())->insertWidget(3, metadataSection.get());
    const auto summarizeMetadata = [&] {
        metadata->apply();
        metadataSource.ShowOptionsString();
        metadataSection->setSummary(
            metadataSource.OptionsString.IsEmpty()
                ? QString()
                : QCoreApplication::translate("ArchiveRedesign", "Modified"));
    };
    connect(metadataSection->button(), &QToolButton::toggled, metadata, summarizeMetadata);
    for (auto *check : metadata->findChildren<QCheckBox *>())
        connect(check, &QCheckBox::clicked, metadata, summarizeMetadata);
    for (auto *combo : metadata->findChildren<QComboBox *>())
        connect(combo, &QComboBox::activated, metadata, summarizeMetadata);
    connect(method_, &QComboBox::currentTextChanged, metadata, [&] {
        metadataSource.MethodID = archiveNameState_.GetMethodID();
        metadata->refreshContext();
        summarizeMetadata();
    });
    summarizeMetadata();
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
    buttons->SetAcceptText("ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Apply"));
    auto *layout = QtCreateDialogLayout(editor, *buttons, QtDialogContent::Scrollable);
    layout->addWidget(expertContent_);
    expertContent_->show();
    connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
        if (parameters_->text().contains(QChar::Null)) {
            ShowErrorMessageQt(&editor, NWindows::NError::MyFormatMessage(E_INVALIDARG));
            parameters_->setFocus();
            return;
        }
        metadata->apply();
        applyMetadataSource(metadataSource, initialMetadataSource, metadataProfile);
        editor.accept();
    });
    editor.resize(width(), 480);
    QTimer::singleShot(0, &editor, [&] {
        QWidget *target = focus ? focus : method_;
        if (focus == parameters_)
            parametersSection_->setExpanded(true);
        else
            compressionSection_->setExpanded(true);
        QtFitScrollableDialog(editor);
        editor.findChild<QScrollArea *>("dialogContentScroll")->ensureWidgetVisible(target);
        if (QApplication::activeModalWidget() != &editor)
            return;
        target->setFocus();
        if (focus == parameters_ && parameters_->text().contains(QChar::Null))
            ShowErrorMessageQt(&editor, NWindows::NError::MyFormatMessage(E_INVALIDARG));
    });
    applied = editor.exec() == QDialog::Accepted;
    if (applied) {
        SaveOptionsInMem();
        updateAdvancedSummary();
    }
}
