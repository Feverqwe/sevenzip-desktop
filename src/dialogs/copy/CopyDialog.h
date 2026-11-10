// Adapted from 7-Zip CPP/7zip/UI/FileManager/CopyDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"

#include <QDialog>
#include <QStringList>
#include <functional>
#include <optional>

class QComboBox;
class QtSourceSummary;
class QLineEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;

struct QtCopyDestinationContext {
    QString SourceDirectory;
    QString SourceName;
    QString PeerDestination;
    bool PeerSupportsWriting = false;
    std::function<bool(const QString &)> CanRename;
    // Null uses the caller's ordinary process-relative filesystem contract.
    QString RelativeBase = {};
    std::function<bool(const QString &)> IsInsideSourceDirectory = {};
    bool PeerIsFilesystem = false;
    bool PeerIsArchive = false;
    bool LocalFileTransfer = false;
    bool IsPeerWritable() const;
};

bool CopyPathMatchesArchivePanel(const QString &path, const QString &panelPath);
QString NormalizeCopyDestination(const QString &path);

const int kCopyDialog_NumInfoLines = 11;

class CCopyDialog : public QDialog {
    Q_OBJECT

  public:
    CCopyDialog(const QString &title, const QString &label, const QString &path,
                const QString &info, QWidget *parent = nullptr);
    CCopyDialog(bool move, const QString &initialPath, const QStringList &sources,
                QWidget *parent = nullptr, const QString &info = QString());
    UString Title;
    UString Static;
    UString Value;
    UString Info;

    QString destination() const;
    void BindLanguage(UInt32 titleID, UInt32 promptID, const QString &titleSuffix = {});
    void accept() override;
    void SetSourceDetails(const QString &location, std::optional<quint64> knownSize);
    void ConfigureDestination(QtCopyDestinationContext context);
    bool usesDestinationName() const { return _acceptedNamedDestination; }

  protected:
    void changeEvent(QEvent *event) override;

  private:
    QComboBox *_path = nullptr;
    QLineEdit *_pathEdit = nullptr;
    QStringList _sources;
    QtSourceSummary *_sourceSummary = nullptr;
    QString _sourceLocation;
    std::optional<quint64> _knownSize;
    bool _copyMove = false;
    bool _move = false;
    bool _acceptedNamedDestination = false;
    std::optional<QtCopyDestinationContext> _destinationContext;
    QLineEdit *_name = nullptr;
    QLabel *_nameLabel = nullptr;
    QLabel *_destinationMessage = nullptr;
    QPushButton *_peer = nullptr;
    bool _canSubmit = true;
    bool _useName = false;
    void BuildDestinationOptions(QVBoxLayout &layout);
    bool UpdateDestination(bool checkBatch = false, QLineEdit **correctionField = nullptr);
    QString DraftDestination() const;
    QString PreviewDestination(const QString &value) const;
    QString BrowseStartDirectory() const;
    void RefreshSourceSummary();
    bool OnInit();
    void OnButtonSetPath();
    void FocusDestinationError(QLineEdit *field);
    void OnOK();
    bool ReadPath(UString &path);
    void SetPathText(const QString &path);
};
