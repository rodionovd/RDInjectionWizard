//
//  RDIWDeamonMaster.m
//  RDInjectionWizard
//
//  Created by Dmitry Rodionov on 7/12/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//
@import ServiceManagement;
@import Security;

#import <xpc/xpc.h>
#import <pthread.h>
#import "RDIWDeamonMaster.h"

#define kRDIWDeamonMasterCallbackQueueLabel "me.rodionovd.RDIWDeamonMaster.callbackqueue"

static NSString *kRDIWDeamonIdentifer = @"me.rodionovd.RDInjectionWizard.injector";

static pthread_mutex_t authorizationLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fileIOLock = PTHREAD_MUTEX_INITIALIZER;

@interface RDIWDeamonMaster()
{
    // A connection handle for our priveleged helper
    xpc_connection_t _connection;
    AuthorizationRef _authorization;
    // A dispatch queue to perform user's callbacks in
    dispatch_queue_t _callbackQueue;
}
- (BOOL)_initializeXPCConnection;
- (BOOL)_copyHelperIntoHostAppBundle;
- (BOOL)_registerDeamonWithLaunchd;
- (BOOL)_removeHelperFromHostAppBundle;
@end

@implementation RDIWDeamonMaster

+ (instancetype)sharedMaster
{
    static id master = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        master = [[[self class] alloc] init];
    });

    return (master);
}

- (instancetype)init
{
    if ((self = [super init])) {
        _callbackQueue = dispatch_queue_create(kRDIWDeamonMasterCallbackQueueLabel,
                                                DISPATCH_QUEUE_CONCURRENT);
    }

    return (self);
}

- (void)tellDeamonToInjectTarget: (pid_t)target
                     withPayload: (NSString *)payload
               completionHandler: (RDIWDaemonConnectionCallback)callback
{
    if (![self _initializeXPCConnection]) {
        dispatch_async(_callbackQueue, ^{
            if (callback) callback(nil, kCouldNotEstabilishXPCConnection);
        });
        return;
    }

    /* Configure a request message */
    xpc_object_t injection_request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(injection_request, "target", target);
    xpc_dictionary_set_string(injection_request, "payload", [payload UTF8String]);

    if (callback) {
        xpc_connection_send_message_with_reply(_connection, injection_request,
                                               _callbackQueue,
                                               ^(xpc_object_t object) {
                                                   callback(object, kSuccess);
                                               });
    } else {
        xpc_connection_send_message(_connection, injection_request);
    }
}


- (BOOL)_initializeXPCConnection
{
    if (_connection) {
        return YES;
    }
    pthread_mutex_lock(&fileIOLock);
    if (NO == [self _copyHelperIntoHostAppBundle]) {
        pthread_mutex_unlock(&fileIOLock);
        return NO;
    }
    if (NO == [self _registerDeamonWithLaunchd]) {
        pthread_mutex_unlock(&fileIOLock);
        return NO;
    }
    if (NO == [self _removeHelperFromHostAppBundle]) {
        pthread_mutex_unlock(&fileIOLock);
        return NO;
    }

    _connection = xpc_connection_create_mach_service([kRDIWDeamonIdentifer UTF8String],
                                                     NULL,
                                                     XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
    if (!_connection) {
        NSLog(@"%s: Unable to create XPC connection", __PRETTY_FUNCTION__);
        pthread_mutex_unlock(&fileIOLock);
        return NO;
    }
    pthread_mutex_unlock(&fileIOLock);

    /* Every connection has to have an event handler set */
    xpc_connection_set_event_handler(_connection, ^(xpc_object_t __unused event) {
        /* no-op */
    });
    xpc_connection_resume(_connection);

    return YES;
}

/**
 * Since we use SMJobBless() to bootstrap our helper deamon with launchd,
 * we need to play by the Apple's rules and store our helper inside a host
 * bundle's `Contents/Library/LaunchServices` subdirectory.
 */
- (BOOL)_copyHelperIntoHostAppBundle
{
    /* Chech if we've already copied the helper */
    NSFileManager *manager = [NSFileManager defaultManager];
    NSString *new_path = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:
                          @"Contents/Library/LaunchServices"];
    new_path = [new_path stringByAppendingPathComponent: kRDIWDeamonIdentifer];
    BOOL daemon_was_copied = [manager fileExistsAtPath: new_path];
    if (daemon_was_copied) {
        return YES;
    }
    /* Locate the helper inside the framework bundle */
    NSBundle *framework_bundle = [NSBundle bundleForClass: self.class];
    if ([framework_bundle bundlePath].length == 0) {
        NSLog(@"%s: Could not locate framework bunlde", __PRETTY_FUNCTION__);
        return NO;
    }
    NSString *orig_path = [framework_bundle.bundlePath stringByAppendingPathComponent:
                           @"LaunchServices"];
    orig_path = [orig_path stringByAppendingPathComponent: kRDIWDeamonIdentifer];
    BOOL original_helper_exists = [manager fileExistsAtPath: orig_path];
    if (!original_helper_exists) {
        NSLog(@"%s: Could not find original helper location", __PRETTY_FUNCTION__);
        return NO;
    }
    /* Check if there's already a Contents/Library/LaunchServices directory */
    NSError *error = nil;
    NSString *launchservices_dir = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:
                                    @"Contents/Library/LaunchServices"];
    BOOL directory_exists = [manager fileExistsAtPath: launchservices_dir];
    if (!directory_exists) {
        BOOL directory_created = [manager createDirectoryAtPath: launchservices_dir
                                    withIntermediateDirectories: YES
                                                     attributes: nil
                                                          error: &error];
        if (!directory_created) {
            NSLog(@"%s: %@", __PRETTY_FUNCTION__, error.localizedDescription);
            return NO;
        }
    }
    /* Copy the helper into the host's bundle */
    error = nil;
    BOOL copied = [manager copyItemAtPath: orig_path toPath: new_path error: &error];
    if (!copied) {
        NSLog(@"%s: %@", __PRETTY_FUNCTION__, error.localizedDescription);
        return NO;
    }

    return YES;
}

- (BOOL)_registerDeamonWithLaunchd
{
    /** Check if the injector deamon is already installed */
    CFDictionaryRef job = SMJobCopyDictionary(kSMDomainSystemLaunchd,
                                              (__bridge CFStringRef)(kRDIWDeamonIdentifer));
    if (job) {
        CFRelease(job);
        return YES;
    }

    pthread_mutex_lock(&authorizationLock);
    {
        /* Requst an authorization right to be able to register (install) our helper tool */
        AuthorizationItem items[] = {
            ((AuthorizationItem){kSMRightBlessPrivilegedHelper, 0, NULL, 0})
        };
        uint32_t items_count = 1;
        AuthorizationRights authRights = {items_count, items};
        AuthorizationFlags flags = (
            kAuthorizationFlagDefaults | kAuthorizationFlagInteractionAllowed |
            kAuthorizationFlagPreAuthorize | kAuthorizationFlagExtendRights
        );
        int status = AuthorizationCreate(&authRights, kAuthorizationEmptyEnvironment,
                                              flags, &_authorization);
        if (status != errAuthorizationSuccess) {
            pthread_mutex_unlock(&authorizationLock);
            NSLog(@"%s: could not create authorization rights!", __PRETTY_FUNCTION__);
            return NO;
        }
    }
    pthread_mutex_unlock(&authorizationLock);

    /* Register the helper with launchd */
    CFErrorRef error = NULL;
    Boolean blessed = SMJobBless(kSMDomainSystemLaunchd,
                                 (__bridge CFStringRef)(kRDIWDeamonIdentifer),
                                 _authorization,
                                 &error);
    if (!blessed) {
        NSLog(@"%s: %@", __PRETTY_FUNCTION__, (__bridge NSError *)error);
    }

    return !!(blessed);
}

- (BOOL)_removeHelperFromHostAppBundle
{
    NSFileManager *manager = [NSFileManager defaultManager];
    NSString *helper_path = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:
                          @"Contents/Library/LaunchServices"];
    helper_path = [helper_path stringByAppendingPathComponent: kRDIWDeamonIdentifer];
    BOOL helper_exists = [manager fileExistsAtPath: helper_path];
    if (!helper_exists) {
        return YES;
    }
    /* Remove the helper itself */
    NSError *error = nil;
    BOOL removed = [manager removeItemAtPath: helper_path error: &error];
    if (!removed) {
        NSLog(@"%s: %@", __PRETTY_FUNCTION__, error);
        return NO;
    }
    /* Remove the Contents/Library/LaunchServices directory if it's empty now */
    NSString *launchServicesDir = [helper_path stringByDeletingLastPathComponent];
    error = nil;
    NSArray *files = [manager contentsOfDirectoryAtPath: launchServicesDir error: &error];
    if (files.count != 0 || error) {
        return YES;
    }
    error = nil;
    if ( ! [manager removeItemAtPath: launchServicesDir error: &error]) {
        NSLog(@"%s: %@", __PRETTY_FUNCTION__, error);
        return NO;
    }
    /* Remove the Contents/Library/ directory if it's empty now */
    NSString *libraryDir = [launchServicesDir stringByDeletingLastPathComponent];
    error = nil;
    files = [manager contentsOfDirectoryAtPath: libraryDir error: &error];
    if (files.count != 0 || error) {
        return YES;
    }
    error = nil;
    if ( ! [manager removeItemAtPath: libraryDir error: &error]) {
        NSLog(@"%s: %@", __PRETTY_FUNCTION__, error);
        return NO;
    }

    return (YES);
}

@end
