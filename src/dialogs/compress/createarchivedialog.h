#pragma once

#include "archive/backend/archivebackend.h"
#include "archive/sfx/archivesfxmodule.h"
#include "dialogs/compress/CompressDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <optional>

class QtDisclosure;
class QScrollArea;

class CreateArchiveDialog : public QDialog {
    Q_OBJECT

  public:
    explicit CreateArchiveDialog(const QString &directory, const QStringList &initialSources = {},
                                 QWidget *parent = nullptr,
                                 std::shared_ptr<const ArchiveCodecCatalog> codecs = {},
                                 const CCompressDialog *sourceDialog = nullptr);

    NCompressDialog::CInfo sourceInfo() const;
    static HRESULT PresentSource(CCompressDialog &source,
                                 const CObjectVector<NWildcard::CCensorPath> &paths,
                                 std::shared_ptr<const ArchiveCodecCatalog> codecs,
                                 QWidget *parent);

    QString target() const;
    QStringList sources() const { return selected_; }
    QString password() const;
    bool encryptHeaders() const;
    CompressionOptions compressionOptions() const;

  private:
    struct AcceptedRequest {
        CompressionOptions Options;
        UInt32 SourceLevel;
    };
    AcceptedRequest readRequest(const QString &target) const;
    void showEvent(QShowEvent *event) override;
    static QScrollArea *createOptionsScrollArea(QWidget *parent);
    QPushButton *createArchiveDestination(QVBoxLayout *windowLayout);
    void createArchiveForm(QVBoxLayout *layout, QVBoxLayout *windowLayout);
    void createOptionSections(QVBoxLayout *layout, QFormLayout *basic);
    void stabilizeColumnWidths();
    static QWidget *reserveControlSpace(QWidget *control);
    void createCompressionOptions(QFormLayout *form);
    void createEncryptionOptions(QVBoxLayout *form);
    void updateRedesignSummary();
    void createVolumeOptions(QFormLayout *form);
    void createAdvancedOptions(QFormLayout *form);
    void createCommandLineOptions(QFormLayout *form);
    void createLifecycleOptions(QFormLayout *form);
    void updateSfxControls();
    void toggleSfx(bool enabled);
    void showAdvancedOptions();
    void prepareMetadataSource(CCompressDialog &source) const;
    void updateAdvancedSummary();
    bool checkVolumeSizes();
    void SetEncryptionMethod();
    QString GetEncryptionMethodSpec() const;
    void UpdatePasswordControl();
    void SetLevel2();
    void FormatChanged();
    void MethodChanged();
    void SetOrder2(UInt32 defaultOrder);
    void SetDictionary2(UInt32 defaultDict);
    void SetSolidBlockSize2(UInt32 defaultBlockSize);
    UInt64 GetDict2() const;
    void SetNumThreads2();
    UInt64 GetMemoryUsage_DecompMem(UInt64 &decompressMemory) const;
    void SetMemoryUsage();
    NCompression::CMemUse Get_MemUse_Spec() const;
    bool checkMemoryUsage();
    int FindRegistryFormat(const QString &name) const;
    unsigned FindRegistryFormat_Always(const QString &name);
    NCompression::CFormatOptions &Get_FormatOptions();
    void SaveOptionsInMem();
    UString parameterOptions() const;
    void SetMemUseCombo();
    static QString compressionSettingsPath();
    void createFooter(QVBoxLayout *layout);
    void BindLanguage();
    void ReloadLanguageItems();
    static QString LevelText(unsigned level);
    void changeEvent(QEvent *event) override;
    void validateAndAccept();
    QString archivePath() const;

    std::optional<AcceptedRequest> accepted_;
    std::shared_ptr<const ArchiveCodecCatalog> codecs_;
    CCompressDialog archiveNameState_;
    void configureArchiveName(const QString &format);
    void populateArchiveFormats();
    void prepareInitialSources(const QStringList &sources);
    void initializeSourceControls(const NCompressDialog::CInfo &info);
    bool fixedSources_ = false;
    QString suggestedArchiveName_ = QStringLiteral("archive");
    QStringList selected_;
    QLineEdit *name_ = nullptr;
    QComboBox *archivePath_ = nullptr;
    QLineEdit *archiveFolder_ = nullptr;
    QComboBox *format_ = nullptr;
    QComboBox *updateMode_ = nullptr;
    QComboBox *pathMode_ = nullptr;
    QComboBox *volume_ = nullptr;
    QLineEdit *parameters_ = nullptr;
    QPushButton *advanced_ = nullptr;
    bool hasMetadataOptions_ = false;
    QCheckBox *sharedFiles_ = nullptr;
    QCheckBox *sfx_ = nullptr;
    ArchiveSfxModule sfxModule_;
    QCheckBox *deleteAfterCompressing_ = nullptr;
    QComboBox *encryption_ = nullptr;
    QComboBox *level_ = nullptr;
    QComboBox *method_ = nullptr;
    QComboBox *dictionary_ = nullptr;
    QComboBox *wordSize_ = nullptr;
    QComboBox *solidBlock_ = nullptr;
    QComboBox *threads_ = nullptr;
    QLabel *hardwareThreads_ = nullptr;
    QLabel *memoryUsage_ = nullptr;
    QComboBox *memUse_ = nullptr;
    NCompression::CInfo m_RegistryInfo;
    QString registryFormat_;
    QLabel *decompressionMemory_ = nullptr;
    const NCompressDialog::CSystemInfo system_ = NCompressDialog::GetSystemInfo();
    QLineEdit *password_ = nullptr;
    QLineEdit *passwordConfirmation_ = nullptr;
    QCheckBox *showPassword_ = nullptr;
    QCheckBox *encryptHeaders_ = nullptr;
    QCheckBox *passwordEnabled_ = nullptr;
    QtDisclosure *fileOptions_ = nullptr;
    QtDisclosure *volumeOptions_ = nullptr;
    QtDisclosure *commandLineOptions_ = nullptr;
    QFormLayout *encryptionForm_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
};
