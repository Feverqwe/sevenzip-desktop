#import <Foundation/Foundation.h>

void PostNativeRegionalChange() {
    [[NSNotificationCenter defaultCenter] postNotificationName:NSCurrentLocaleDidChangeNotification
                                                        object:nil];
}
