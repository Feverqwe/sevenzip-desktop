#pragma once
#include "filemanager/views/filetable.h"
#include <QCoreApplication>
#include <QMimeDatabase>

// Main-window list presentation around the shared source-bound table/model.
// Provider values, sorting and operations retain their native owners.
class QtWorkspaceFileTable final : public FileTable {
    Q_DECLARE_TR_FUNCTIONS(QtWorkspaceFileTable)
  public:
    QtWorkspaceFileTable(const QString &settingsPath, QWidget *parent = nullptr);
    static void DefaultColumns(const CPanel &panel, CPropColumns &columns);
    void configureSourceColumns() override;
    QString typeLabel(const QString &name, bool directory) const override;
    void appendPresentationColumnActions(QMenu &menu) override;
    void sourceColumnVisibilityChanged() override;
    void invalidateLanguageText() override;

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void FitName();
    void SavePresentation();
    void RefreshTypeCaption();
    void RestoreTypeOrder(int position);
    QString settingsPath_;
    QString group_;
    bool configuring_ = false;
    bool autoName_ = false;
    bool typeAvailable_ = false;
    QMimeDatabase mimeTypes_;
};
