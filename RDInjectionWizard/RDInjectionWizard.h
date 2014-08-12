//
//  RDInjectionWizard.h
//  RDInjectionWizard
//
//  Created by Dmitry Rodionov on 7/12/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//
#import <Foundation/Foundation.h>

typedef enum {
    kInvalidProcessIdentifier,
    kInvalidPayload,
    kCouldNotCopyPayload,
    kCouldNotConnectToHelper,
    kInjectionFailed
} RDInjectionError;

typedef void (^RDInjectionSuccededBlock)(void);
typedef void (^RDInjectionFailedBlock)(RDInjectionError error);

@interface RDInjectionWizard : NSObject

/// A target process' identifer
@property (assign, readwrite) pid_t target;
/// A path to the payload library/framework
@property (copy, readwrite) NSString *payload;

/**
 * @abstract
 * Initialize an injection wizard for a given target and payload filepath.
 *
 * @param target  an identifer of the target process
 * @param payload a full path to the payload library
 *
 * @return some magic
 */
- (instancetype)initWithTarget: (pid_t)target payload: (NSString *)payload;

/**
 * @abstract
 * Asynchronously inject the given target with the given payload library.
 *
 * @param success
 * The block to be executed on the completion of the injection.
 * It takes no arguments and has no return value.
 * @param failure
 * the block to be executed upon an injection error.
 * It has no return value and takes one argument: an error that occured during the injection.
 */

- (void)injectUsingCompletionBlockWithSuccess: (RDInjectionSuccededBlock) success
                                      failure: (RDInjectionFailedBlock) failure;
/**
 * @abstract
 * Looks up a sandbox-friendly location of the payload.
 *
 * @discussion
 * Use this method if you want to copy the payload into a location within the target's
 * sandbox contaner that is said to be accessible by definition.
 *
 * @return a new unique filename for the payload inside the target's sandbox container
 */
- (NSString *)sandboxFriendlyPayloadPath;

@end
