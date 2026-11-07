// Splitter policy adapted from 7-Zip CPP/7zip/UI/FileManager/FM.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include <QSplitter>

inline constexpr int kSplitterWidth = 4;
inline constexpr int kSplitterRateMax = 1 << 16;

// FM.cpp's original splitter policy. A client width replaces HWND/GetClientRect.
class CSplitterPos {
    int _ratio = 0;
    int _pos = 0;
    int _fullWidth = 0;
    void SetRatioFromPos(int width);
    void SetPosPure(int width, int pos);

  public:
    int GetPos() const { return _pos; }
    void SetRatio(int width, int aRatio);
    void SetPos(int width, int pos);
    void SetPosFromRatio(int width);
};

// Qt owns handle input and child layout around the original coordinate/ratio policy.
class QtFileManagerSplitter final : public QSplitter {
  public:
    explicit QtFileManagerSplitter(QWidget *parent);
    void Initialize(bool panelsDefined, unsigned splitterPos);
    void UpdateSourceLayout();
    void RememberPosition();
    int SourcePosition() const;

  protected:
    QSplitterHandle *createHandle() override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    bool HasTwoPanels() const;
    int ContentWidth() const;
    CSplitterPos splitter_;
    bool initialized_ = false;
    int appliedWidth_ = -1;
    int appliedPosition_ = 0;
    int appliedLeftSize_ = 0;
    bool appliedTwoPanels_ = false;
};
