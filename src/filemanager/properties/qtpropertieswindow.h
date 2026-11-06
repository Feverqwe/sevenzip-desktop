#pragma once
#include "filemanager/properties/qtpropertyreport.h"
#include "ui/controls/qtmetadatarows.h"
#include <QDialog>
#include <QPointer>

class QLabel;
class QtSourceSummary;
class QtDisclosure;
class QVBoxLayout;

class QtPropertiesWindow final : public QDialog {
    Q_OBJECT
  public:
    explicit QtPropertiesWindow(QtPropertyReport report, QWidget *parent = nullptr,
                                QWidget *returnFocus = nullptr);
    QString Information() const;
    void done(int result) override;

  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    struct ValueRow {
        unsigned source;
        QtPropertyReport::Scope scope;
        QtMetadataRows::Row controls;
    };
    struct Section {
        qsizetype group;
        bool comments;
        QtDisclosure *control;
    };
    QtPropertyReport report_;
    QPointer<QWidget> returnFocus_;
    QtSourceSummary *summary_ = nullptr;
    QLabel *location_ = nullptr, *size_ = nullptr, *modified_ = nullptr, *selectedNames_ = nullptr;
    QList<ValueRow> rows_;
    QList<Section> sections_;
    bool firstShow_ = true;
    QSize initialFrame_;
    void BuildGroup(qsizetype index, bool comments, QVBoxLayout &layout);
    void Refresh();
    QString SummaryName() const;
    QString SummaryType() const;
    QString SummarySize() const;
    QString SummaryModified() const;
    QString SummaryLocation() const;
    QString Caption(unsigned row, QtPropertyReport::Scope scope) const;
    QString Value(unsigned row) const;
    QString GroupTitle(const QtPropertyReport::Group &group, bool comments) const;
};
