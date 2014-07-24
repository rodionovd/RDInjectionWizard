//
//  RDInjectionWizard.m
//  RDInjectionWizard
//
//  Created by Dmitry Rodionov on 7/12/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//
#import <sys/param.h>
#import "RDIWDeamonMaster.h"
#import "RDInjectionWizard.h"

#define kRDFallbackSandboxContainer ([@"~/Library/Fonts" stringByExpandingTildeInPath])
#define kRDInjectionWizardCallbackQueueLabel "me.rodionovd.RDInjectionWizard.callbackqueue"
@interface RDInjectionWizard()
{
    /* A dispatch queue to perform user's callbacks on */
    dispatch_queue_t _callbackQueue;
}

@end

@implementation RDInjectionWizard

- (instancetype)initWithTarget: (pid_t)target payload: (NSString *)payload
{
    if ((self = [super init])) {
        _payload = payload;
        _target = target;
        _callbackQueue = dispatch_queue_create(kRDInjectionWizardCallbackQueueLabel, DISPATCH_QUEUE_CONCURRENT);
    }
    return (self);
}

- (void)injectUsingCompletionBlockWithSuccess: (RDInjectionSuccededBlock) success
                                      failure: (RDInjectionFailedBlock) failure
{
    if (_target < 0) {
        dispatch_async(_callbackQueue, ^{
            if (failure) failure(kInvalidProcessIdentifier);
        });
        return;
    }

    BOOL _payload_exists = [[NSFileManager defaultManager] fileExistsAtPath: _payload];
    if (!_payload_exists) {
        dispatch_async(_callbackQueue, ^{
            if (failure) failure(kInvalidPayload);
        });
        return;
    }

    /* If our payload is a framework/bundle, use its executable path as a payload */
    NSBundle *bundle = [NSBundle bundleWithPath: _payload];
    if (bundle && [bundle.executablePath length] > 0) {
        [self setPayload: bundle.executablePath];
    }

    NSString *sandbox_friendly_payload = [self sandboxFriendlyPayloadPath];
    if ([sandbox_friendly_payload length] == 0) {
        dispatch_async(_callbackQueue, ^{
            if (failure) failure(kInvalidPayload);
        });
        return;
    }

    BOOL payload_was_copied = NO;
    if (NO == [sandbox_friendly_payload isEqualToString: _payload]) {
        payload_was_copied = YES;
        NSError *error = nil;
        BOOL copied = [[NSFileManager defaultManager] copyItemAtPath: _payload
                                                              toPath: sandbox_friendly_payload
                                                               error: &error];
        if (!copied) {
            NSLog(@"%s: %@", __PRETTY_FUNCTION__, error);
            dispatch_async(_callbackQueue, ^{
                if (failure) failure(kInvalidPayload);
            });
            return;
        }
    }

    RDIWDeamonMaster *master = [RDIWDeamonMaster sharedMaster];
    [master tellDeamonToInjectTarget: _target
                         withPayload: sandbox_friendly_payload
                   completionHandler:
     ^(xpc_object_t reply, RDIWConnectionError error) {
         /* Remove a payload copy if any */
         if (payload_was_copied) {
             [[NSFileManager defaultManager] removeItemAtPath: sandbox_friendly_payload
                                                        error: nil];
         }
         /* Check the xpc reply for an injection error */
         if (!reply) {
             NSLog(@"%s: daemon connection error: %d", __PRETTY_FUNCTION__, error);
             dispatch_async(_callbackQueue, ^{
                 if (failure) failure(kCouldNotConnectToHelper);
             });
             return;
         }
         BOOL status = xpc_dictionary_get_bool(reply, "status");
         if (status) {
             dispatch_async(_callbackQueue, ^{
                 if (success) success();
             });
         } else {
             dispatch_async(_callbackQueue, ^{
                 if (failure) failure(kInjectionFailed);
             });
         }
     }];

    return;
}

/* Apple's private API for checking sandbox status */
extern int sandbox_check(pid_t pid, const char *operation, int type, ...);
extern int sandbox_container_path_for_pid(pid_t, char *buffer, size_t bufsize);

- (NSString *)sandboxFriendlyPayloadPath
{
    if (sandbox_check(_target, NULL, 0) == 0) {
        return _payload;
    }

    char *buf = malloc(sizeof(*buf) * MAXPATHLEN);
    if (!buf) {
        return _payload;
    }
    NSString *container = nil;
    if (sandbox_container_path_for_pid(_target, buf, MAXPATHLEN) == KERN_SUCCESS) {
        container = [NSString stringWithUTF8String: buf];
    }
    free(buf);

    if (container.length == 0) {
        container = kRDFallbackSandboxContainer;
    }
    NSString *unique_filename = [NSString stringWithFormat: @"%@.%f",
                                 [_payload lastPathComponent],
                                 [NSDate timeIntervalSinceReferenceDate]];

    return [container stringByAppendingPathComponent: unique_filename];
}

@end
