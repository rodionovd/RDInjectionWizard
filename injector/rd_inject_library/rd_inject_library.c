//
//  rd_inject_library.c
//  rd_inject_library
//
//  Created by Dmitry Rodionov on 5/31/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <stdbool.h>
#include <pthread.h>
#include <mach/mach.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <dispatch/dispatch.h>
#include <mach/machine/thread_status.h>

#include "rd_inject_library.h"

#define kRDRemoteStackSize      (25*1024)
#define kRDShouldJumpToDlopen   0xabad1dea

#define RDFailOnError(function) {if (err != KERN_SUCCESS) {syslog(LOG_NOTICE, "[%d] %s failed with error: %s\n", \
    __LINE__-1, function, mach_error_string(err)); return (err);}}

#pragma mark - Private Interface

static int load_library_into_task(task_t task, const char *library_path, void **return_value);
static mach_port_t
init_exception_port_for_thread(thread_act_t thread, thread_state_flavor_t thread_flavor);
static bool process_is_64_bit(pid_t proc);

/* Received from xnu/bsd/uxkern/ux_exception.c
 * (Apple's XNU version 2422.90.20)
 */
typedef struct {
    mach_msg_header_t Head;
    /* start of the kernel processed data */
    mach_msg_body_t msgh_body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    /* end of the kernel processed data */
    NDR_record_t NDR;
    exception_type_t exception;
    mach_msg_type_number_t codeCnt;
    mach_exception_data_t code;
    /* some times RCV_TO_LARGE probs */
    char pad[512];
} exc_msg_t;

#pragma mark - Implementation

int rd_inject_library(pid_t target_proc, const char *library_path)
{
    int err = KERN_FAILURE;
    if (target_proc <= 0 || !library_path) {
        return (err);
    }

    /* Yeah, I know, there're lots of i386 apps.
     * No.
     * There aren't.
     * YOLO.
     */
    bool proc64bit = process_is_64_bit(target_proc);
    if (!proc64bit) {
        syslog(LOG_NOTICE, "[The target task should be a 64 bit process]");
        goto end;
    }

    task_t task;
    /* You should be a member of procmod users group in order to
     * use task_for_pid(). Being root is OK. */
    err = task_for_pid(mach_task_self(), target_proc, &task);
    if (err != KERN_SUCCESS) {
        syslog(LOG_NOTICE, "task_for_pid() failed with error: %s [%d]",
               mach_error_string(err), err);
        goto end;
    }
    void *remote_dlopen_return_value = NULL;
    err = load_library_into_task(task, library_path, &remote_dlopen_return_value);
    if (err != KERN_SUCCESS) {
        syslog(LOG_NOTICE, "load_library_into_task() failed with error: %s [%d]",
               mach_error_string(err), err);
        goto end;
    }
    if (remote_dlopen_return_value == NULL) {
        err = KERN_INVALID_OBJECT;
        syslog(LOG_NOTICE, "Remote dlopen() failed");
        goto end;
    }

end:
    return (err);
}

/**
 * @abstract
 * Check wheither the given task is a 64 bit process.
 *
 * @return YES
 * Means the process' architecture is x86_64 (also ppcp64, but who cares)
 * @return NO
 * Means it's not
 */
static
bool process_is_64_bit(pid_t proc)
{
    int mib[4] = {
        CTL_KERN, KERN_PROC, KERN_PROC_PID, proc
    };
    struct kinfo_proc info;
    size_t size = sizeof(info);

    if (sysctl(mib, 4, &info, &size, NULL, 0) == KERN_SUCCESS) {
        return (info.kp_proc.p_flag & P_LP64);
    } else {
        return false;
    }
}

/**
 * @abstract
 * Load a library into a given task.
 *
 * @discussion
 * This function creates a remote thread inside the task and perform dlopen()
 * on this thread.
 *
 * As the created thead is a plain mach thread, we also need to convert it into a UNIX
 * pthead before calling dlopen(). To do that we'll set up an exeption handler for the
 * remote thread, then call _pthread_set_self() with invalid return address on it, catch
 * an EXC_BAD_ACCESS exception, re-configure the thread to call dlopen() and resume it.
 * We'll also gracefully terminate the thread when dlopen() returned.
 *
 * @return
 * KERN_SUCCESS if injection was done without errors
 * @return
 * KERN_FAILRUE if there're some injection errors
 */
static
int load_library_into_task(task_t task, const char *library_path, void **return_value)
{
    if (!task) return KERN_INVALID_ARGUMENT;
    int err = KERN_FAILURE;

    /* Copy the library path into target's address space */
    size_t path_size = strlen(library_path) + 1;
    mach_vm_address_t rlibrary = 0;
    err = mach_vm_allocate(task, &rlibrary, path_size, VM_FLAGS_ANYWHERE);
    RDFailOnError("mach_vm_allocate");
    err = mach_vm_write(task, rlibrary, (vm_offset_t)library_path,
                        (mach_msg_type_number_t)path_size);
    RDFailOnError("mach_vm_write");

    /* Compose a fake backtrace and allocate remote stack. */
    uint64_t fake_backtrace[] = {
        kRDShouldJumpToDlopen
    };
    mach_vm_address_t stack = 0;
    err = mach_vm_allocate(task, &stack, (kRDRemoteStackSize + sizeof(fake_backtrace)),
                           VM_FLAGS_ANYWHERE);
    RDFailOnError("mach_vm_allocate");

    /* Reserve some place for a pthread struct */
    mach_vm_address_t pthread_struct = stack;

    /* Copy the backtrace to the top of remote stack */
    err = mach_vm_write(task, (stack + kRDRemoteStackSize), (vm_offset_t)fake_backtrace,
                        (mach_msg_type_number_t)sizeof(fake_backtrace));
    RDFailOnError("mach_vm_write");

    /* Initialize a remote thread state */
    x86_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    state.__rbp = stack;
    state.__rsp = stack + kRDRemoteStackSize;

    /* We'll jump right into _pthread_set_self() to convert
     * our mach thread into real POSIX thread */
    void *pthread_set_self = dlsym(RTLD_DEFAULT, "_pthread_set_self");
    if (!pthread_set_self) {
        err = KERN_INVALID_HOST;
        RDFailOnError("dlsym(\"_pthread_set_self\")");
    }
    state.__rip = (mach_vm_address_t)pthread_set_self;
    state.__rdi = pthread_struct;
    /* Store the library path here for dlopen() */
    state.__rbx = rlibrary;

    /* Create a remote thread, set up an exception port for it
     * (so we will be able to handle ECX_BAD_ACCESS exceptions) */
    thread_act_t remote_thread = 0;
    err = thread_create(task, &remote_thread);
    RDFailOnError("thead_create");
    err = thread_set_state(remote_thread, x86_THREAD_STATE64,
                           (thread_state_t)&state, x86_THREAD_STATE64_COUNT);
    RDFailOnError("thread_set_state");

    mach_port_t exception_port = 0;
    exception_port = init_exception_port_for_thread(remote_thread, x86_THREAD_STATE64);
    if (!exception_port) {
        err = KERN_FAILURE;
        RDFailOnError("init_exception_handler_for_thread");
    }
    err = thread_resume(remote_thread);
    RDFailOnError("thread_resume");

    /* Exception handling loop */
    while (1) {
        /* Use exc_server() as a generic server for sending/receiving exception messages */
        extern boolean_t exc_server(mach_msg_header_t *request, mach_msg_header_t *reply);
        err = mach_msg_server_once(exc_server, sizeof(exc_msg_t), exception_port, 0);
        RDFailOnError("mach_msg_server_once");

        thread_basic_info_data_t thread_basic_info;
        mach_msg_type_number_t thread_basic_info_count = THREAD_BASIC_INFO_COUNT;
        err = thread_info(remote_thread, THREAD_BASIC_INFO,
                          (thread_info_t)&thread_basic_info, &thread_basic_info_count);
        RDFailOnError("thread_info");

        /* Chech if we've already suspended the thread inside our exception handler */
        if (thread_basic_info.suspend_count > 0) {
            /* So retrive the dlopen() return value first */
            if (return_value) {
                x86_thread_state64_t updated_state;
                mach_msg_type_number_t state_count = x86_THREAD_STATE64_COUNT;
                err = thread_get_state(remote_thread, x86_THREAD_STATE64,
                                       (thread_state_t)&updated_state, &state_count);
                RDFailOnError("thread_get_state");
                *return_value = (void *)updated_state.__rax;
            }

            /* then terminate the remote thread */
            err = thread_terminate(remote_thread);
            RDFailOnError("thead_terminate");
            /* and do some memory clean-up */
            err = mach_vm_deallocate(task, rlibrary, path_size);
            RDFailOnError("mach_vm_deallocate");
            err = mach_vm_deallocate(task, stack, kRDRemoteStackSize);
            RDFailOnError("mach_vm_deallocate");
            err = mach_port_deallocate(mach_task_self(), exception_port);
            RDFailOnError("mach_port_deallocate");
            break;
        }
    }

    return err;
}


/**
 * @abstract
 * Initialize an EXC_BAD_ACCESS exception port for the given thread, and set it
 * for this thread.
 *
 * @return
 * An identifier of the initialized exception port for catching EXC_BAD_ACCESS
 * @return
 * (0) that means the kernel failed to initizalize the exception port for the given thread
 */
static
mach_port_t init_exception_port_for_thread(thread_act_t thread, thread_state_flavor_t thread_flavor)
{
    int err = KERN_FAILURE;
    /* Allocate an exception port */
    mach_port_name_t exception_port;
    err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exception_port);
    if (err != KERN_SUCCESS) return (0);
    err = mach_port_insert_right(mach_task_self(), exception_port, exception_port,
                                 MACH_MSG_TYPE_MAKE_SEND);
    if (err != KERN_SUCCESS) return (0);
    /* Assign this port to our thread */
    err = thread_set_exception_ports(thread, EXC_MASK_ALL,
                                     exception_port, EXCEPTION_STATE_IDENTITY,
                                     thread_flavor);
    if (err != KERN_SUCCESS) return (0);

    return exception_port;
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
/**
 * @abstract
 * Custom EXC_BAD_ACCESS exception handler, called by exc_server().
 *
 * @return
 * KERN_SUCCESS indicates that we've reset a thread state and it's ready to be run again
 * @return
 * MIG_NO_REPLY indicates that we've terminated or suspended the thread, so the kernel won't
 *              handle it anymore. This one also breaks our exception handling loop, so we
 *              can finish the injection process
 * @return
 * KERN_FAILURE indicates that we was not expect this kind of exception and the kernel should
 *              find another handler for it
 *
 */
__attribute__((visibility("default")))
kern_return_t
catch_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread,
                                     mach_port_t task, exception_type_t exception,
                                     exception_data_t code, mach_msg_type_number_t code_count,
                                     int *flavor, thread_state_t in_state,
                                     mach_msg_type_number_t in_state_count,
                                     thread_state_t out_state,
                                     mach_msg_type_number_t *out_state_count)
{
#pragma unused (exception_port, task)
#pragma unused (exception, code, code_count, in_state_count, out_state, out_state_count)

    if (*flavor != x86_THREAD_STATE64) {
        return KERN_FAILURE;
    }

    if (((x86_thread_state64_t *)in_state)->__rip == kRDShouldJumpToDlopen) {
        /* Prepare the thread to execute dlopen() */
        ((x86_thread_state64_t *)out_state)->__rip = (uint64_t)&dlopen;
        ((x86_thread_state64_t *)out_state)->__rsi = RTLD_NOW | RTLD_LOCAL;
        ((x86_thread_state64_t *)out_state)->__rdi = ((x86_thread_state64_t *)in_state)->__rbx;
        /* Preserve the stack pointer */
        uint64_t stack = ((x86_thread_state64_t *)in_state)->__rsp;
        ((x86_thread_state64_t *)out_state)->__rsp =  stack;
        /* Indicate that we've updateed this thread state and ready to resume it */
        *out_state_count = x86_THREAD_STATE64_COUNT;

        return KERN_SUCCESS;
    }
    /* In any other case we want to gracefully terminate the thread, so our
     * target won't crash. */
    thread_suspend(thread);

    return MIG_NO_REPLY;
}
#pragma clang diagnostic pop // ignored "-Wmissing-prototypes"
