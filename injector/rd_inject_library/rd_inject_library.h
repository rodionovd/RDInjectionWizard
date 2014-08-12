//
//  rd_inject_library.h
//  rd_inject_library
//
//  Created by Dmitry Rodionov on 5/31/14.
//  Copyright (c) 2014 rodionovd. All rights reserved.
//

#pragma once

/**
 * @abstract
 * Loads (injects) a dynamic library into a target process.
 *
 * @discussion
 * This function creates new thread in the target process and runs dlopen() on it.
 *
 * @see internal load_library_into_task() for details
 *
 * @param target
 * The identifer of the target process
 * @param library_path
 * The full path of the library to be injected
 *
 * @return KERN_SUCCESS
 * Means the completion of injection and library loading
 * @return KERN_INVALID_OBJECT
 * Means that the remote dlopen() failed to open the library
 * @return KERN_FAILURE
 * Means an error occured while injecting into the target
 */
int rd_inject_library(pid_t target, const char *library_path);
