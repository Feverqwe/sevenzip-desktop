#include "filemanager/panels/qtmacfilepromisedrag.h"
#import <AppKit/AppKit.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMimeData>
#include <QScopedValueRollback>
#include <QUuid>
#include <QWidget>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <private/qmacmimeregistry_p.h>

namespace {
Q_LOGGING_CATEGORY(promiseLog, "sevenzip.mac.filepromise", QtWarningMsg)
constexpr auto mimeType = "application/x-sevenzip-promise-drag";
NSString *const pasteboardType = @"com.trolltech.anymime.application--x-sevenzip-promise-drag";
struct LocalDrag {
    QByteArray Token;
    void *Source;
};
const LocalDrag *activeDrag = nullptr;
NSEvent *lastDragEvent = nil;
} // namespace

@interface SevenzipPromiseWriter : NSObject <NSFilePromiseProviderDelegate> {
  @public
    std::function<bool()> prepare;
    std::function<void()> cleanup;
    NSOperationQueue *writeQueue;
}
@end

@interface SevenzipPromiseProvider : NSFilePromiseProvider {
  @public
    SevenzipPromiseWriter *writer;
    NSURL *sourceURL;
    NSData *token;
}
@end

@implementation SevenzipPromiseProvider
- (void)dealloc {
    [writer release];
    [sourceURL release];
    [token release];
    [super dealloc];
}
- (NSArray<NSPasteboardType> *)writableTypesForPasteboard:(NSPasteboard *)pasteboard {
    return [[super writableTypesForPasteboard:pasteboard] arrayByAddingObject:pasteboardType];
}
- (id)pasteboardPropertyListForType:(NSPasteboardType)type {
    return [type isEqualToString:pasteboardType] ? token
                                                 : [super pasteboardPropertyListForType:type];
}
@end

@implementation SevenzipPromiseWriter
- (instancetype)init {
    self = [super init];
    if (self) {
        writeQueue = [[NSOperationQueue alloc] init];
        writeQueue.maxConcurrentOperationCount = 1;
    }
    return self;
}
- (void)dealloc {
    if ([NSThread isMainThread]) {
        if (cleanup)
            cleanup();
    } else {
        // The callback owns the original archive provider. Release it in the
        // same thread that attached it to the source panel.
        const auto releaseSource = std::move(prepare);
        const auto finish = std::move(cleanup);
        dispatch_async(dispatch_get_main_queue(), ^{
          Q_UNUSED(releaseSource);
          if (finish)
              finish();
        });
    }
    [writeQueue release];
    [super dealloc];
}
- (NSOperationQueue *)operationQueueForFilePromiseProvider:(NSFilePromiseProvider *)provider {
    Q_UNUSED(provider);
    return writeQueue;
}
- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)provider
                  fileNameForType:(NSString *)type {
    Q_UNUSED(type);
    qCDebug(promiseLog) << "Receiver requested file name";
    return ((SevenzipPromiseProvider *)provider)->sourceURL.lastPathComponent;
}
- (void)filePromiseProvider:(NSFilePromiseProvider *)provider
          writePromiseToURL:(NSURL *)url
          completionHandler:(void (^)(NSError *))completion {
    qCDebug(promiseLog) << "Receiver requested write";
    bool prepared = false;
    const auto materialize = [&] {
        qCDebug(promiseLog) << "Preparing source in Qt";
        prepared = prepare();
        qCDebug(promiseLog) << "Source preparation finished" << prepared;
    };
    if ([NSThread isMainThread]) {
        materialize();
    } else if (auto *app = QCoreApplication::instance(); app && !QCoreApplication::closingDown()) {
        QMetaObject::invokeMethod(app, materialize, Qt::BlockingQueuedConnection);
    }
    if (!prepared) {
        qCDebug(promiseLog) << "Source preparation failed";
        completion([NSError errorWithDomain:NSCocoaErrorDomain
                                       code:NSFileReadUnknownError
                                   userInfo:nil]);
        return;
    }
    NSURL *input = ((SevenzipPromiseProvider *)provider)->sourceURL;
    // AppKit invokes the delegate under its destination write claim.
    // Coordinating the same URL here waits on that enclosing claim.
    NSError *error = nil;
    [[NSFileManager defaultManager] copyItemAtURL:input toURL:url error:&error];
    qCDebug(promiseLog) << "Write finished, error"
                        << (error ? QString::fromNSString(error.domain) : QString())
                        << (error ? error.code : 0);
    completion(error);
}
@end

@interface SevenzipPromiseDragSource : NSObject <NSDraggingSource> {
  @public
    QEventLoop *loop;
    bool accepted;
    bool finished;
}
@end

@implementation SevenzipPromiseDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    Q_UNUSED(session);
    Q_UNUSED(context);
    return NSDragOperationCopy;
}
- (void)draggingSession:(NSDraggingSession *)session
           endedAtPoint:(NSPoint)point
              operation:(NSDragOperation)operation {
    Q_UNUSED(session);
    Q_UNUSED(point);
    accepted = (operation & NSDragOperationCopy) != 0;
    qCDebug(promiseLog) << "Drag finished, operation" << qulonglong(operation);
    finished = true;
    if (loop)
        loop->quit();
}
@end

void QtRegisterMacFilePromiseDrag() {
    static id monitor = [] {
        QMacMimeRegistry::registerDraggedTypes({QString::fromNSString(pasteboardType)});
        return [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                                             NSEventMaskLeftMouseDragged |
                                                             NSEventMaskLeftMouseUp
                                                     handler:^NSEvent *(NSEvent *event) {
                                                       [lastDragEvent release];
                                                       lastDragEvent =
                                                           event.type == NSEventTypeLeftMouseDragged
                                                               ? [event copy]
                                                               : nil;
                                                       return event;
                                                     }];
    }();
    Q_UNUSED(monitor);
}

void *QtMacFilePromiseDragSource(const QMimeData &mime) {
    return activeDrag && mime.data(mimeType) == activeDrag->Token ? activeDrag->Source : nullptr;
}

std::optional<bool> QtRunMacFilePromiseDrag(QWidget &widget, const QList<QtMacPromisedItem> &items,
                                            void *source, const std::function<bool()> &prepare,
                                            const std::function<void()> &cleanup) {
    if (activeDrag || items.isEmpty() || !lastDragEvent) {
        qCDebug(promiseLog) << "Skipping native drag, active" << bool(activeDrag) << "items"
                            << items.size() << "event" << bool(lastDragEvent);
        return std::nullopt;
    }
    NSView *view = reinterpret_cast<NSView *>(widget.window()->winId());
    if (!view || view.window != lastDragEvent.window) {
        qCDebug(promiseLog) << "Skipping native drag, event belongs to another window";
        return std::nullopt;
    }
    NSEvent *event = [lastDragEvent autorelease];
    lastDragEvent = nil;

    QEventLoop loop;
    const LocalDrag local{QUuid::createUuid().toRfc4122(), source};
    QScopedValueRollback<const LocalDrag *> active(activeDrag, &local);
    SevenzipPromiseDragSource *dragSource = [[[SevenzipPromiseDragSource alloc] init] autorelease];
    dragSource->loop = &loop;
    SevenzipPromiseWriter *writer = [[[SevenzipPromiseWriter alloc] init] autorelease];
    writer->prepare = prepare;
    auto *dragItems = [NSMutableArray<NSDraggingItem *> arrayWithCapacity:items.size()];
    const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    for (const auto &item : items) {
        UTType *type =
            item.Directory
                ? UTTypeFolder
                : [UTType typeWithFilenameExtension:QFileInfo(item.Path).suffix().toNSString()];
        if (!type)
            type = UTTypeData;
        SevenzipPromiseProvider *provider =
            [[[SevenzipPromiseProvider alloc] initWithFileType:type.identifier
                                                      delegate:writer] autorelease];
        provider->writer = [writer retain];
        provider->sourceURL = [[NSURL fileURLWithPath:item.Path.toNSString()] retain];
        provider->token = [[NSData alloc] initWithBytes:local.Token.constData()
                                                 length:local.Token.size()];
        NSDraggingItem *dragItem =
            [[[NSDraggingItem alloc] initWithPasteboardWriter:provider] autorelease];
        NSImage *icon = [NSWorkspace.sharedWorkspace iconForContentType:type];
        [dragItem setDraggingFrame:NSMakeRect(point.x - 16, point.y - 16, 32, 32) contents:icon];
        [dragItems addObject:dragItem];
    }
    if (![view beginDraggingSessionWithItems:dragItems event:event source:dragSource]) {
        qCDebug(promiseLog) << "AppKit did not start drag";
        return std::nullopt;
    }
    qCDebug(promiseLog) << "AppKit drag started";
    writer->cleanup = cleanup;
    if (!dragSource->finished)
        loop.exec();
    dragSource->loop = nullptr;
    return dragSource->accepted;
}
