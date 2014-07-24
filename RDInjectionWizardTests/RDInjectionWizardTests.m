//
//  RDInjectionWizardTests.m
//  RDInjectionWizardTests
//
//  Created by Dmitry Rodionov on 7/12/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <Cocoa/Cocoa.h>
#import "RDInjectionWizard.h"


@interface RDInjectionWizardTests : XCTestCase

@end

@implementation RDInjectionWizardTests

- (void)setUp
{
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown
{
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}



- (void)testInjectionInto64BitSandboxedTarget
{
    NSString *libnoop64 = [[NSBundle mainBundle] pathForResource: @"libtestnoop64" ofType: @"dylib"];
    XCTAssert(libnoop64.length > 0);

    /* Use Mail.app as a target */
    [[NSWorkspace sharedWorkspace] launchApplication: @"Mail"];
    NSRunningApplication *mail = nil;
    do {
        mail = [[NSRunningApplication runningApplicationsWithBundleIdentifier:
                 @"com.apple.mail"] firstObject];
    } while (!mail);
    sleep(1); // let em initialize a bit
    XCTAssert(mail.processIdentifier > 0);

    RDInjectionWizard *wizard = [[RDInjectionWizard alloc] initWithTarget: mail.processIdentifier
                                                                  payload: libnoop64];
    __block BOOL fired = NO;
    XCTestExpectation *callbackFiredExpectation = [self expectationWithDescription: @"callback fired"];
    [wizard injectUsingCompletionBlockWithSuccess: ^{
        fired = YES;
        [callbackFiredExpectation fulfill];
    } failrue: ^(RDInjectionError error) {
        fired = YES;
        XCTFail(@"This test should pass, but here we've got an error: %d", error);
        [callbackFiredExpectation fulfill];
    }];

    [self waitForExpectationsWithTimeout: 10
                                 handler:
     ^(NSError *error) {
         [mail terminate];
         XCTAssertNil(error);
         XCTAssert(fired);
     }];
}

- (void)testInjectionInto64BitTarget
{
    NSURL *target_url = [[NSBundle mainBundle] URLForResource: @"demoTarget64" withExtension: @"app"];
    XCTAssert(target_url.absoluteString.length > 0);

    [[NSWorkspace sharedWorkspace] launchApplication: @"demoTarget64"];
    NSRunningApplication *target64 = nil;
    do {
        target64 = [[NSRunningApplication runningApplicationsWithBundleIdentifier:
                 @"me.rodionovd.RDInjectionWizard.demoTarget64"] firstObject];
    } while (!target64);
    sleep(1); // let em initialize a bit
    XCTAssert(target64.processIdentifier > 0);

    NSString *libnoop64 = [[NSBundle mainBundle] pathForResource: @"libtestnoop64" ofType: @"dylib"];
    XCTAssert(libnoop64.length > 0);

    RDInjectionWizard *wizard = [[RDInjectionWizard alloc] initWithTarget: target64.processIdentifier
                                                                  payload: libnoop64];
    __block BOOL fired = NO;
    XCTestExpectation *callbackFiredExpectation = [self expectationWithDescription: @"callback fired"];
    [wizard injectUsingCompletionBlockWithSuccess: ^{
        fired = YES;
        [callbackFiredExpectation fulfill];
    } failrue: ^(RDInjectionError error) {
        fired = YES;
        XCTFail(@"This test should pass, but here we've got an error: %d", error);
        [callbackFiredExpectation fulfill];
    }];

    [self waitForExpectationsWithTimeout: 10
                                 handler:
     ^(NSError *error) {
         [target64 terminate];
         XCTAssertNil(error);
         XCTAssert(fired);
     }];

}

- (void)testInvalidPayload
{
    XCTestExpectation *callbackFiredExpectation = [self expectationWithDescription: @"callback fired"];
    RDInjectionWizard *wizard = [[RDInjectionWizard alloc] initWithTarget: getpid()
                                                                  payload: @"invalid"];
    __block BOOL fired = NO;
    [wizard injectUsingCompletionBlockWithSuccess: ^{
        fired = YES;
        XCTFail(@"This test should not succeed");
        [callbackFiredExpectation fulfill];
    } failrue: ^(RDInjectionError error) {
        fired = YES;
        XCTAssert(error == kInvalidPayload);
        [callbackFiredExpectation fulfill];
    }];

    [self waitForExpectationsWithTimeout: 10
                                 handler:
     ^(NSError *error) {
         XCTAssert(fired);
     }];
}

- (void)testInvalidPid
{
    XCTestExpectation *callbackFiredExpectation = [self expectationWithDescription: @"callback fired"];
    pid_t invalid_target = -123;
    NSString *valid_payload = [[NSBundle bundleForClass:
                                      NSClassFromString(@"RDInjectionWizard")] bundlePath];
    XCTAssert(valid_payload.length > 0);

    RDInjectionWizard *wizard = [[RDInjectionWizard alloc] initWithTarget: invalid_target
                                                                  payload: valid_payload];
    __block BOOL fired = NO;
    [wizard injectUsingCompletionBlockWithSuccess: ^{
        fired = YES;
        XCTFail(@"This test should not succeed");
        [callbackFiredExpectation fulfill];

    } failrue: ^(RDInjectionError error) {
        fired = YES;
        XCTAssert(error == kInvalidProcessIdentifier);
        [callbackFiredExpectation fulfill];
    }];

    [self waitForExpectationsWithTimeout: 10
                                 handler:
     ^(NSError *error) {
         XCTAssert(fired);
     }];
}


@end
