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
#define kDefaultIdleTime (dispatch_time(DISPATCH_TIME_NOW, kIdleExitTimeoutSec * NSEC_PER_SEC))
#define kDeamonIdentifer "me.rodionovd.RDInjectionWizard.injector"

static dispatch_source_t idle_exit_timer = NULL;

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

            /* Pause an idle-exit timer */
            dispatch_source_set_timer(idle_exit_timer, DISPATCH_TIME_FOREVER, 0, 0);
            {
                bool success = main_routine(event);
                xpc_connection_t remote = xpc_dictionary_get_remote_connection(event);
                xpc_object_t reply = xpc_dictionary_create_reply(event);
                xpc_dictionary_set_bool(reply, "status", success);
                xpc_connection_send_message(remote, reply);
                xpc_release(reply);
            }
            /* Reset the timer */
            dispatch_source_set_timer(idle_exit_timer, kDefaultIdleTime, 0, 0);
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
    idle_exit_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                          dispatch_get_main_queue());
    if (!idle_exit_timer) {
        syslog(LOG_NOTICE, "Failed to create an idle-exit timer source.");
        exit(EXIT_FAILURE);
    }
    dispatch_set_context(idle_exit_timer, NULL);
    dispatch_source_set_event_handler_f(idle_exit_timer, (void (*)(void *))exit);
    dispatch_source_set_timer(idle_exit_timer, kDefaultIdleTime, 0, 0);
    dispatch_resume(idle_exit_timer);

    xpc_connection_resume(service);
    dispatch_main();
    xpc_release(service);

    return EXIT_SUCCESS;
}
