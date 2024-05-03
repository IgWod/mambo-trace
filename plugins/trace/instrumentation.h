/*
    Copyright 2024 Igor Wodiany
    Copyright 2024 The University of Manchester

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <pthread.h>
#include <stdint.h>

#include "../../plugins.h"

// CONSTANTS

/*
    Maximum number of unique threads spawns that can be traced. A thread spawn is not
    unique if it differs in an entry_addr or a call_site from other thread spawns.
*/
#define NUMBER_THREAD_ENTRIES 32 

// TYPEDEFS

struct lift_thread_data;
typedef struct lift_thread_data lift_thread_data;

struct lift_thread_metadata;
typedef struct lift_thread_metadata lift_thread_metadata;

struct lift_plugin_data;
typedef struct lift_plugin_data lift_plugin_data;

// STRUCTS

/*
    Data stored in the thread private memory.
*/
struct lift_thread_data {
    mambo_ht_t* cfg; // The control flow graph (CFG) in the form of a look-up table with all the nodes in the CFG.
                     // We use the hashmap to keep track of all the nodes while the application is running. We only
                     // connect nodes with each other after the instrumented application finishes execution.
    void* current_block_address; // Address of the last encountered basic block.
    uint64_t block_id; // Counter that tracks the order of the execution of basic blocks.
};

/*
    Data structure used to track threads created by the running application.
*/
struct lift_thread_metadata {
    void* entry_addr; // Address of the thread start routine.
    void* call_site; // Address of the function call (branch-link) to the function starting the new thread.
};

/*
    Data stored in the global memory.
*/
struct lift_plugin_data {
    void* main_addr; // Address of the main function recovered from __libc_start_main. NOTE: Has to be the first field
                     // for the instrumentation to work correctly.

    mambo_ht_t* cfg; // Global CFG - for more information see lift_thread_data.

    pthread_mutex_t lock; // Lock that needs to be acquired to modify the global data.

    lift_thread_metadata threads_entries[NUMBER_THREAD_ENTRIES]; // Data related to spawned threads.

    void* current_call_addr; // Keep track of the most recent address of a function call (branch-link). This is later
                             // used to relate new threads to the location where they were spawned.
};
