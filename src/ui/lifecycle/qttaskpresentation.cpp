#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QTimer>
#include <QWidget>

QtTaskPresentation::QtTaskPresentation(QObject *parent) : QObject(parent) {
    setObjectName(QStringLiteral("sevenzipTaskPresentation"));
    qApp->installEventFilter(this);
}

QtTaskPresentation &QtTaskPresentation::Application() {
    for (auto *object : qApp->children())
        if (auto *presentation = qobject_cast<QtTaskPresentation *>(object))
            return *presentation;
    return *new QtTaskPresentation(qApp);
}

QtTaskPresentation::QuestionScope::QuestionScope() : owner_(&Application()) {
    ++owner_->questionDepth_;
    owner_->Publish();
}
QtTaskPresentation::QuestionScope::~QuestionScope() {
    if (owner_) {
        --owner_->questionDepth_;
        owner_->Publish();
    }
}

void QtTaskPresentation::Watch(QWidget &window, Phase phase) {
    auto &owner = Application();
    for (auto &entry : owner.entries_)
        if (entry.window == &window) {
            entry.phase = phase;
            owner.Publish();
            return;
        }
    owner.entries_.append({&window, phase, window.isVisible() ? ++owner.sequence_ : 0});
    connect(&window, &QObject::destroyed, &owner, [&owner] {
        owner.entries_.removeIf([](const Entry &entry) { return !entry.window; });
        owner.Publish();
    });
    owner.Publish();
}

void QtTaskPresentation::SetPhase(QWidget &window, Phase phase) { Watch(window, phase); }

void QtTaskPresentation::BeginWork(QWidget &window) {
    auto &owner = Application();
    owner.workStarted_ = true;
    owner.resultReviewed_ = false;
    owner.fallback_ = Phase::Working;
    Watch(window, Phase::Working);
}

void QtTaskPresentation::ResultReviewed() { Application().resultReviewed_ = true; }

bool QtTaskPresentation::CanPresent(const QWidget *owner) {
    const auto *modal = QApplication::activeModalWidget();
    return !Application().questionDepth_ && (!modal || modal == owner) &&
           !qobject_cast<QLineEdit *>(QApplication::focusWidget());
}

const QtTaskPresentation::Entry *QtTaskPresentation::currentEntry() const {
    const Entry *current = nullptr;
    for (const auto &entry : entries_)
        if (entry.window && entry.window->isVisible() && (!current || entry.order > current->order))
            current = &entry;
    return current;
}

QWidget *QtTaskPresentation::currentWindow() const {
    if (auto *modal = QApplication::activeModalWidget())
        return modal;
    const auto *entry = currentEntry();
    return entry ? entry->window.data() : nullptr;
}

QtTaskPresentation::Snapshot QtTaskPresentation::snapshot() const {
    auto *window = currentWindow();
    if (questionDepth_)
        return {Phase::Question, window ? window->windowTitle() : QString(), bool(window)};
    if (!window)
        return {fallback_, {}, false};
    for (const auto &entry : entries_)
        if (entry.window == window)
            return {entry.phase, window->windowTitle(), true};
    // A native picker or conflict remains a question owned by its dialog.
    return {Phase::Question, window->windowTitle(), true};
}

void QtTaskPresentation::Publish() {
    const auto state = snapshot();
    if (state.phase == Phase::Working)
        fallback_ = Phase::Working;
    if (state != published_) {
        published_ = state;
        emit changed();
    }
    if (showPending_ && state.hasWindow)
        QTimer::singleShot(0, this, &QtTaskPresentation::Show);
}

void QtTaskPresentation::Show() {
    auto *window = currentWindow();
    showPending_ = !window;
    if (!window)
        return;
    for (auto *parent = window->parentWidget(); parent; parent = parent->parentWidget())
        if (parent->isWindow() && parent->isMinimized())
            parent->showNormal();
    if (window->isMinimized())
        window->showNormal();
    window->raise();
    window->activateWindow();
}

bool QtTaskPresentation::eventFilter(QObject *object, QEvent *event) {
    const auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || !widget->isWindow())
        return false;
    switch (event->type()) {
    case QEvent::Show:
        for (auto &entry : entries_)
            if (entry.window == object)
                entry.order = ++sequence_;
        [[fallthrough]];
    case QEvent::Hide:
    case QEvent::WindowTitleChange:
        // Qt updates the modal stack as part of showing/hiding the window.
        QTimer::singleShot(0, this, &QtTaskPresentation::Publish);
        break;
    default:
        break;
    }
    return false;
}
