//
//  main.c
//  injector
//
//  Created by Dmitry Rodionov on 7/13/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//

#include <syslog.h>
#include <xpc/xpc.h>
#include "rd_inject_library.h"

#define kIdleExitTimeoutSec (10)
#define kDeamonIdentifer "me.rodionovd.RDInjectionWizard.injector"

static bool main_routine(xpc_object_t dictionary)
{
    pid_t target = (pid_t)xpc_dictionary_get_int64(dictionary, "target");
    if (target <= 0) {
        return false;
    }
    const char *payload_path = xpc_dictionary_get_string(dictionary, "payload");
    syslog(LOG_NOTICE, "Inject (%d) <- [%s] ", target, payload_path);
    if (!payload_path) {
        return false;
    }
    return (KERN_SUCCESS == rd_inject_library(target, payload_path));
}

static void __XPC_Connection_Handler(xpc_connection_t connection)  {
	xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        if (xpc_get_type(event) != XPC_TYPE_ERROR) {

            bool success = main_routine(event);
            xpc_connection_t remote = xpc_dictionary_get_remote_connection(event);
            xpc_object_t reply = xpc_dictionary_create_reply(event);
            xpc_dictionary_set_bool(reply, "status", success);
            xpc_connection_send_message(remote, reply);
            xpc_release(reply);
        } else {
            // error handling?
        }
	});

	xpc_connection_resume(connection);
}

int main(__unused int argc, __unused const char *argv[]) {
    xpc_connection_t service = xpc_connection_create_mach_service(kDeamonIdentifer,
                                                                  dispatch_get_main_queue(),
                                                                  XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!service) {
        syslog(LOG_NOTICE, "Failed to create an XPC service.");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_NOTICE, "Configuring connection event handler for helper");
    xpc_connection_set_event_handler(service, ^(xpc_object_t connection) {
        __XPC_Connection_Handler(connection);
    });

    /* Set up an idle-exit timer. @see kIdleExitTimeoutSec for timeout value. */
    dispatch_source_t timer_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                                            dispatch_get_main_queue());
    if (!timer_source) {
        syslog(LOG_NOTICE, "Failed to create an idle-exit timer source.");
        exit(EXIT_FAILURE);
    }
    dispatch_set_context(timer_source, NULL);
    dispatch_source_set_event_handler_f(timer_source, (void (*)(void *))exit);
    dispatch_time_t time = dispatch_time(DISPATCH_TIME_NOW, kIdleExitTimeoutSec * NSEC_PER_SEC);
    dispatch_source_set_timer(timer_source, time, 0, 0);
    dispatch_resume(timer_source);

    xpc_connection_resume(service);
    dispatch_main();
    xpc_release(service);

    return EXIT_SUCCESS;
}
