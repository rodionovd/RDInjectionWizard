//
//  RDIWDaemonMasterTests.m
//  RDInjectionWizard
//
//  Created by Dmitry Rodionov on 7/13/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//

#import <XCTest/XCTest.h>
#import "RDIWDeamonMaster.h"

@interface RDIWDaemonMasterTests : XCTestCase

@end

@implementation RDIWDaemonMasterTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testConnection
{
    RDIWDeamonMaster *master = [RDIWDeamonMaster sharedMaster];
    XCTestExpectation *callbackFiredExpectation = [self expectationWithDescription: @"callback fired"];

    [master tellDeamonToInjectTarget: getpid()
                         withPayload: @"placeholder"
                   completionHandler:
     ^(xpc_object_t reply, RDIWConnectionError error) {

         XCTAssert(error == kSuccess);
         XCTAssert(reply);
         if (reply) {
             // It's just a connection test, so we don't care wheither it successed or fail
//             BOOL success = xpc_dictionary_get_bool(reply, "status");
//             XCTAssertTrue(success);
         }
         [callbackFiredExpectation fulfill];
     }];

    [self waitForExpectationsWithTimeout: 10 handler: ^(NSError *error) {
        XCTAssertNil(error);
    }];
}

@end
