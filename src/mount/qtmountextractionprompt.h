#pragma once
#include "mount/archivemountdecision.h"
#include <QDialog>

// Extraction consent belongs to one mount; unverified-data consent stays per file.
class QtMountExtractionPrompt final : public QDialog {
    Q_OBJECT
    ArchiveMountDecisionPtr decision_;

  public:
    QtMountExtractionPrompt(const QString &source, const QString &path, quint64 size,
                            ArchiveMountDecisionPtr decision, QWidget *parent = nullptr,
                            bool unverified = false);
    ~QtMountExtractionPrompt() override;
};
