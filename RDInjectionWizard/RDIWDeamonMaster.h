//
//  RDIWDeamonMaster.h
//  RDInjectionWizard
//
//  Created by Dmitry Rodionov on 7/12/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//

typedef enum {
    kSuccess,
    kCouldNotEstabilishXPCConnection
} RDIWConnectionError;

typedef void (^RDIWDaemonConnectionCallback)(id reply, RDIWConnectionError error);

@interface RDIWDeamonMaster : NSObject
/**
 * @abstract
 * One-of-a-kind deamon master.
 */
+ (instancetype)sharedMaster __attribute__((const));

/**
 * @abstract
 * Asynchronously sends a "plese, inject this into that" message to the privileged
 * injector helper.
 *
 * @discussion
 * This method doesn't perform any verification of input parameters, so be careful.
 *
 * @param target  a target process for injection
 * @param payload a payload library filename
 * @param callback    a block to be called upon error or when helper's reply received
 */
- (void)tellDeamonToInjectTarget: (pid_t)target
                     withPayload: (NSString *)payload
               completionHandler: (RDIWDaemonConnectionCallback)callback;

@end
