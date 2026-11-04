#include "localization/qtlangcontrols.h"
#include "localization/LangUtils.h"
#include "localization/qtlangdialogcontrols.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QTextLayout>
#include <QVariant>
#include <QWidget>

QString QtLangText(UInt32 id, const QString &englishFallback) {
    const UString s = LangString(id);
    return s.IsEmpty() ? englishFallback : QString::fromWCharArray(s);
}

void QtBindMemoryLimitUnitText(QObject *object, bool ramDefined, quint64 ramGB) {
    if (ramDefined)
        QtBindLocalText(object, "MemoryUnits", QT_TRANSLATE_NOOP("MemoryUnits", "GB / %1 GB (RAM)"),
                        false, {QLocale().toString(ramGB)});
    else
        QtBindLocalText(object, "MemoryUnits", QT_TRANSLATE_NOOP("MemoryUnits", "GB"));
}

QString QtWrapControlText(const QWidget &widget, const QString &text) {
    const int wrapWidth = widget.property("languageWrapWidth").toInt();
    if (wrapWidth <= 0)
        return text;
    QTextLayout layout(text, widget.font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    QStringList lines;
    layout.beginLayout();
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(wrapWidth);
        lines.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return lines.join('\n');
}

namespace {
class QtLangCaption final : public IQtLangDialogControls, public IQtLangWindowText {
    UInt32 id_;
    QString text_;

  public:
    QtLangCaption(UInt32 id, const QString &fallback) : id_(id), text_(fallback) {}
    bool HasItem(UInt32 id) const override { return id == id_; }
    void SetItemText(UInt32 id, const wchar_t *text) override {
        if (HasItem(id))
            SetText(text);
    }
    void SetText(const wchar_t *text) override { text_ = QString::fromWCharArray(text); }
    const QString &Text() const { return text_; }
};

QString TranslateProperty(QObject *object, const char *property, const char *contextProperty,
                          bool withArguments) {
    const QByteArray context = object->property(contextProperty).toByteArray();
    const QByteArray source = object->property(property).toByteArray();
    QString text = QCoreApplication::translate(context.constData(), source.constData());
    if (withArguments)
        for (const QString &argument : object->property("languageArguments").toStringList())
            text = text.arg(argument);
    return text;
}

QString ControlCaption(QObject *object, const QVariant &id) {
    QString text = id.isValid()
                       ? QtLangText(id.toUInt(), object->property("languageFallback").toString())
                       : TranslateProperty(object, "languageLocalEnglish", "languageContext", true);
    if (id.isValid()) {
        const UInt32 languageID = id.toUInt();
        const QVariant resource = object->property("languageResourceCaption");
        const QVariant standard = object->property("languageStandardControl");
        QtLangCaption caption(standard.isValid() ? standard.toUInt() : languageID,
                              resource.isValid() ? resource.toString() : text);
        if (standard.isValid())
            LangSetDlgItems(caption, nullptr, 0);
        else if (object->property("languageColon").toBool())
            LangSetDlgItems_Colon(caption, &languageID, 1);
        else if (object->property("languageRemoveColon").toBool())
            LangSetDlgItems_RemoveColon(caption, &languageID, 1);
        else if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow())
            LangSetWindowText(caption, languageID);
        else if (resource.isValid())
            LangSetDlgItemText(caption, languageID, languageID);
        text = caption.Text();
    }
    return text;
}

void SetText(QObject *object) {
    if (auto *widget = qobject_cast<QWidget *>(object))
        if (object->property("languageToolTipEnglish").isValid())
            widget->setToolTip(TranslateProperty(object, "languageToolTipEnglish",
                                                 "languageToolTipContext", false));
    const QVariant id = object->property("languageID");
    const QVariant local = object->property("languageLocalEnglish");
    if (!id.isValid() && !local.isValid())
        return;
    QString text = ControlCaption(object, id);
    text += object->property("languageSuffix").toString();
    if (auto *widget = qobject_cast<QWidget *>(object))
        text = QtWrapControlText(*widget, text);
    if (auto *widget = qobject_cast<QWidget *>(object)) {
        if (object->property("languageToolTip").toBool())
            widget->setToolTip(QString(text).remove('&'));
        if (object->property("languagePlaceholder").toBool())
            if (auto *edit = qobject_cast<QLineEdit *>(widget))
                edit->setPlaceholderText(QString(text).remove('&'));
    }
    if (object->property("languageAccessibleOnly").toBool()) {
        if (auto *widget = qobject_cast<QWidget *>(object)) {
            QString name = text;
            name.remove('&');
            widget->setAccessibleName(name);
        }
        return;
    }
    if (auto *action = qobject_cast<QAction *>(object))
        action->setText(text);
    else if (auto *label = qobject_cast<QLabel *>(object))
        label->setText(text);
    else if (auto *button = qobject_cast<QAbstractButton *>(object))
        button->setText(text);
    else if (auto *group = qobject_cast<QGroupBox *>(object))
        group->setTitle(text);
    else if (auto *widget = qobject_cast<QWidget *>(object))
        widget->setWindowTitle(text);
}
class QtLangTextFilter final : public QObject {
  public:
    explicit QtLangTextFilter(QWidget *window) : QObject(window) {
        setObjectName("qtLangTextFilter");
        window->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::LanguageChange)
            if (auto *window = qobject_cast<QWidget *>(object))
                QtReloadLangTexts(*window);
        return QObject::eventFilter(object, event);
    }
};
void InstallLanguageFilter(QObject *object) {
    if (auto *widget = qobject_cast<QWidget *>(object)) {
        QWidget *window = widget->window();
        if (!window->findChild<QObject *>("qtLangTextFilter", Qt::FindDirectChildrenOnly))
            new QtLangTextFilter(window);
    }
}
void BindTextProperties(QObject *object, UInt32 id, const QString &fallback, bool resource,
                        bool accessibleOnly) {
    object->setProperty("languageID", id);
    object->setProperty("languageResourceCaption", resource ? QVariant(fallback) : QVariant());
    object->setProperty("languageFallback", fallback);
    object->setProperty("languageAccessibleOnly", accessibleOnly);
    object->setProperty("languageStandardControl", QVariant());
    InstallLanguageFilter(object);
}
} // namespace

void QtBindLangText(QObject *object, UInt32 id, const QString &englishFallback,
                    bool accessibleOnly) {
    BindTextProperties(object, id, englishFallback, false, accessibleOnly);
    SetText(object);
}

void QtBindResourceText(QObject *object, UInt32 id, const QString &resourceCaption,
                        bool accessibleOnly) {
    BindTextProperties(object, id, resourceCaption, true, accessibleOnly);
    SetText(object);
}

void QtBindStandardResourceText(QObject *object, UInt32 controlID, const QString &resourceCaption) {
    BindTextProperties(object, controlID, resourceCaption, true, false);
    object->setProperty("languageStandardControl", controlID);
    SetText(object);
}

void QtBindLocalText(QObject *object, const char *context, const char *sourceText,
                     bool accessibleOnly, const QStringList &arguments) {
    object->setProperty("languageLocalEnglish", QByteArray(sourceText));
    object->setProperty("languageContext", QByteArray(context));
    object->setProperty("languageArguments", arguments);
    object->setProperty("languageID", QVariant());
    object->setProperty("languageAccessibleOnly", accessibleOnly);
    InstallLanguageFilter(object);
    SetText(object);
}

void QtBindLocalToolTip(QObject *object, const char *context, const char *sourceText) {
    object->setProperty("languageToolTipEnglish", QByteArray(sourceText));
    object->setProperty("languageToolTipContext", QByteArray(context));
    InstallLanguageFilter(object);
    SetText(object);
}

void QtReloadLangTexts(QWidget &root) {
    SetText(&root);
    for (QObject *object : root.findChildren<QObject *>())
        SetText(object);
}

void QtNotifyLanguageChanged() {
    // Qt has no Win32 property-sheet/menu reload message. Notify current
    // windows after the source language is applied; pending input is untouched.
    for (QWidget *window : QApplication::topLevelWidgets()) {
        QEvent event(QEvent::LanguageChange);
        QApplication::sendEvent(window, &event);
    }
}
