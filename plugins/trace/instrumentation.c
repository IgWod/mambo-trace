/*
    Copyright 2021-2024 Igor Wodiany
    Copyright 2021-2024 The University of Manchester

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

#ifdef PLUGINS_NEW

#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "cfg.h"
#include "save.h"

#include "instrumentation.h"

/* 
    Enables checks for NULL pointers (WARNING: May introduce a small performance
    degradation, but makes the application to fail gracefully, and allows
    debugging of any potential problems).
*/
// #define ALLOW_CRITICAL_PATH_CHECKS 

/*
    Number of indirect branches that can be tracked. Exceeding this number causes
    an undefined behaviour within the lifter and some data may be lost. This is
    intentional as we avoid any dynamic allocation during the control flow
    recovery to improve the overall performance. Changing this value without
    consulting instrumentation.S will result in an incorrect execution.
*/
#define NUMBER_INDIRECT_TARGETS 4096 

/*
    Used for programs compiled with GNU libc (Linux default).
*/
#define RECOVER_MAIN_ADDR_GLIBC

/*
    Load main address from the symbol table. Only works with non-stripped binaries.
*/
#define LOAD_MAIN_ADDR 

/*
    Enable support for multi-threaded applications. This introduces a performance degradation
    as extra instrumentation has to be added to track an address of the most recent function
    call. For now it only support sequential control programs, i.e, only the main thread can
    spawn new threads.
*/
// #define THREADS_SUPPORT 

#ifdef THREADS_SUPPORT
    /*
        Enable support for pthreads applications.
    */
    #define PTHREADS_SUPPORT

    /*
        Enable support for OpenMP applications. Enabling OpenMP and pthreads support will result
        in excessive lifting, as pthreads calls from the OpenMP runtime will be followed alongside
        GOMP_parallel.
    */
    #define OPENMP_SUPPORT
#endif

/*
    Measure execution times of various parts of the lifter. Results in extra prints to stderr.
*/
#define PERFORMANCE_MONITORING 

#ifdef PERFORMANCE_MONITORING
    #include "aarch64_utils.h"
#endif

#ifdef PERFORMANCE_MONITORING
struct timers {
    uint64_t dynamic_execution;
} timers;
#endif

/*
    Store target of an indirect branch into a hash map. Function implemented directly in assembly to increase the
    performance and avoid registers spilling. See instrumentation.S.
*/
void track_branch_target(void *target_address, cfg_edge *edge);

/*
    Allocate per thread data for the newly entered thread.
*/
int lift_pre_thread_cb(mambo_context *ctx) {
    int ret;

    lift_thread_data *thread_data = (lift_thread_data *) mambo_alloc(ctx, sizeof(lift_thread_data));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (thread_data == NULL) {
        fprintf(stderr, "mclift: Couldn't allocate thread data on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif

    thread_data->cfg = (mambo_ht_t *) mambo_alloc(ctx, sizeof(mambo_ht_t));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (thread_data->cfg == NULL) {
        fprintf(stderr, "mclift: Couldn't allocate the hash map on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif

    ret = mambo_ht_init(thread_data->cfg, 1 << 20, 0, 80, false);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Couldn't initialize the hash map on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif

    thread_data->block_id = 0;

    ret = mambo_set_thread_plugin_data(ctx, (void *) thread_data);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Couldn't set the thread data on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif
}

/*
    Merge thread data into the global data and clean-up thread data.
*/
int lift_post_thread_cb(mambo_context *ctx) {
    int ret;

    lift_thread_data *thread_data = (lift_thread_data *) mambo_get_thread_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (thread_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the thread data on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif

    // We can get the data pointer without locking, but we need to acquire the lock to make any modifications.
    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
        exit(-1);
    }
#endif

    ret = pthread_mutex_lock(&plugin_data->lock);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Failed to lock the mutex!\n");
        exit(-1);
    }
#endif

    // Merge thread data into the global hash map.
    for (int index = 0; index < thread_data->cfg->size; index++) {
        if (thread_data->cfg->entries[index].key != 0) {
            cfg_node *local_node = (cfg_node *) thread_data->cfg->entries[index].value;
            cfg_node *global_node = NULL;

            ret = mambo_ht_get_nolock(plugin_data->cfg, (uintptr_t) local_node->start_addr, (void *) (&global_node));
            if (ret) {
                mambo_ht_add_nolock(plugin_data->cfg, (uintptr_t) local_node->start_addr, (uintptr_t) local_node);
            }
        }
    }

    ret = pthread_mutex_unlock(&plugin_data->lock);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Failed to unlock the mutex!\n");
        exit(-1);
    }
#endif

    mambo_free(ctx, thread_data->cfg);
    mambo_free(ctx, thread_data);
}

/*
    Run the lifter and clean-up any global data.
*/
int lift_exit_cb(mambo_context *ctx) {
    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
        exit(-1);
    }
#endif

#ifdef PERFORMANCE_MONITORING
    fprintf(stderr, "We're done; Finished after %lfs\n",
            (double) (get_virtual_counter() - timers.dynamic_execution) / (double) get_virtual_counter_frequency());
#endif

    save(ctx, plugin_data->cfg, plugin_data->main_addr, plugin_data->threads_entries);

    mambo_free(ctx, plugin_data->cfg);
    mambo_free(ctx, plugin_data);
}

/*
    Find end of the basic block and instrument branches if needed.
*/
int lift_pre_inst_cb(mambo_context *ctx) {
    int ret;

    void *inst_source_address = mambo_get_source_addr(ctx);

    mambo_branch_type branch_type = mambo_get_branch_type(ctx);

    uint32_t inst = *(uint32_t *) inst_source_address;

    // Beside checking the branch type we also check whether the instruction is SVC or BRK. We use this to avoid calling
    // PIE for every instruction as it may degrade the performance:
    // * (inst & 0xffe0001f) == 0xd4000001 checks whether the instruction is SVC.
    // * (inst & 0xffe0001f) == 0xd4200000 checks whether the instruction is BRK.
    if (((branch_type != BRANCH_NONE) || (inst & 0xffe0001f) == 0xd4000001 || (inst & 0xffe0001f) == 0xd4200000)) {

        a64_instruction inst_type = a64_decode(inst_source_address);

        lift_thread_data *thread_data = (lift_thread_data *) mambo_get_thread_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
        if (thread_data == NULL) {
            fprintf(stderr, "mclift: Couldn't get the thread data on thread %d!\n",
                    mambo_get_thread_id(ctx));
            exit(-1);
        }
#endif
        void *block_source_address = thread_data->current_block_address;

        cfg_node *node;
        ret = mambo_ht_get_nolock(thread_data->cfg, (uintptr_t) block_source_address, (void *) &node);

        bool is_trace = false;

        if (!ret) {
            node->profile = (cfg_node_profile) mambo_get_fragment_type(ctx);
            is_trace = true;
        }

        // If node forms part of the trace then don't add it again.
        if(!is_trace) {
            node = (cfg_node *) mambo_alloc(ctx, sizeof(cfg_node));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (node == NULL) {
                fprintf(stderr, "mclift: Couldn't allocate the node on thread %d!\n",
                        mambo_get_thread_id(ctx));
                exit(-1);
            }
#endif
            initialize_node(node);

            node->start_addr = block_source_address;
            node->end_addr = inst_source_address;
            // TODO: This may be thread unsafe and cause problems with multi-thread applications.
            node->order_id = thread_data->block_id++;

            ret = mambo_ht_add_nolock(thread_data->cfg, (uintptr_t) block_source_address, (uintptr_t) node);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (ret) {
                fprintf(stderr, "mclift: Couldn't add entry to the hash map on thread %d!\n",
                        mambo_get_thread_id(ctx));
                exit(-1);
            }
#endif
        }

        // TOOD: Avoid using is_trace in the if statements.
        if (!is_trace && inst_type == A64_SVC) {
            // SVC - We can recover SVC code statically, so only count executions
            cfg_edge *edge = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (edge == NULL) {
                fprintf(stderr, "mclift: Couldn't allocate the edge on thread %d!\n",
                        mambo_get_thread_id(ctx));
                exit(-1);
            }
#endif
            initialize_edge(edge, CFG_EDGE_NOTYPE);

            node->edges = edge;

            node->type = CFG_SVC;
        } else if(!is_trace && inst_type == A64_BRK) {
            // BRK - For now just treat as a regular basic block that leads to nowhere
            node->edges = NULL;

            node->type = CFG_BASIC_BLOCK;
        } else if (branch_type & BRANCH_INDIRECT) {
            // BR, BLR, RET - Branches are indirect so to recover targets we need to instrument them
            cfg_edge *edges = NULL;

            if(!is_trace) {
                // Create a linked list of targets (edges) to store targets of indirect branches.
                edges = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge) * NUMBER_INDIRECT_TARGETS);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
                if (edges == NULL) {
                    fprintf(stderr, "mclift: Couldn't allocate the edge on thread %d!\n",
                            mambo_get_thread_id(ctx));
                    exit(-1);
                }
#endif
                for (int idx = 0; idx < NUMBER_INDIRECT_TARGETS - 1; idx++) {
                    initialize_edge(&edges[idx], CFG_EDGE_NOTYPE);
                    edges[idx].next = &edges[idx + 1];
                }

                initialize_edge(&edges[NUMBER_INDIRECT_TARGETS - 1], CFG_EDGE_NOTYPE);

                node->edges = edges;
            } else {
                // For traces, we just continue appending to the same list. Re-initialising it would cause lose of data.
                edges = node->edges;
            }

            unsigned int rn;

            switch (inst_type) {
                case A64_BR:
                    a64_BR_decode_fields(inst_source_address, &rn);
                    node->type = CFG_INDIRECT_BLOCK;
                    break;
                case A64_BLR:
                    a64_BLR_decode_fields(inst_source_address, &rn);
                    node->type = CFG_INDIRECT_BLOCK | CFG_FUNCTION_CALL;
                    break;
                case A64_RET:
                    a64_RET_decode_fields(inst_source_address, &rn);
                    node->type = CFG_RETURN;
                    break;
                default:
                    fprintf(stderr, "mclift: Cannot instrument unknown indirect branch type %d\n", inst_type);
                    exit(-1);
            }

            node->branch_reg = rn;

            // Instrument code to save the value of the jump target
            emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x8) | (1 << x9) | (1 << x10) | (1 << lr));
            emit_mov(ctx, x0, rn);
            emit_set_reg_ptr(ctx, x1, edges);
            emit_fcall(ctx, track_branch_target);
            emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x8) | (1 << x9) | (1 << x10) | (1 << lr));
        } else if (!is_trace && (branch_type & BRANCH_COND)) {
            // B.cond, TBZ, CBZ - We can recover targets of those branches statically, so we only count executions
            cfg_edge *skipped = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge));
            initialize_edge(skipped, CFG_SKIPPED_BRANCH);

            cfg_edge *taken = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge));
            initialize_edge(taken, CFG_TAKEN_BRANCH);

            taken->next = skipped;

            node->edges = taken;

            node->type = CFG_CONDITIONAL_BLOCK;
        } else if (!is_trace && (branch_type & BRANCH_CALL)) {
            // BL - We can recover target of this branch statically, so we only count executions
            cfg_edge *edge = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (edge == NULL) {
                fprintf(stderr, "mclift: Couldn't allocate the edge on thread %d!\n",
                        mambo_get_thread_id(ctx));
                exit(-1);
            }
#endif
            initialize_edge(edge, CFG_EDGE_NOTYPE);

            node->edges = edge;

#ifdef THREADS_SUPPORT
            lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (plugin_data == NULL) {
                fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
                exit(-1);
            }
#endif
            emit_push(ctx, (1 << x0) | (1 << x1));
            emit_set_reg(ctx, x0, (uintptr_t) inst_source_address);
            emit_set_reg(ctx, x1, (uintptr_t) &plugin_data->current_call_addr);
            emit_a64_LDR_STR_unsigned_immed(ctx, 3, 0, 0, 0, x1, x0);
            emit_pop(ctx, (1 << x0) | (1 << x1));
#endif

            node->type = CFG_FUNCTION_CALL;
        } else if (!is_trace && (branch_type & BRANCH_DIRECT)) {
            // B - We can recover target of this branch statically, so we only count executions
            cfg_edge *edge = (cfg_edge *) mambo_alloc(ctx, sizeof(cfg_edge));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
            if (edge == NULL) {
                fprintf(stderr, "mclift: Couldn't allocate the edge on thread %d!\n",
                        mambo_get_thread_id(ctx));
                exit(-1);
            }
#endif
            initialize_edge(edge, CFG_EDGE_NOTYPE);

            node->edges = edge;

            node->type = CFG_BASIC_BLOCK;
        } else if(!is_trace){
            fprintf(stderr, "mclift: Branch type %d not supported!\n", inst_type);
            exit(-1);
        }
    }
}

/*
    Get start address of the current basic block.
*/
int lift_pre_basic_block_cb(mambo_context *ctx) {

    void *source_address = mambo_get_source_addr(ctx);

    lift_thread_data *thread_data = (lift_thread_data *) mambo_get_thread_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (thread_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the thread data on thread %d!\n",
                mambo_get_thread_id(ctx));
        exit(-1);
    }
#endif

    thread_data->current_block_address = source_address;
}

/*
    Instrumentation of the thread creation function capturing the thread creating call site and the address of the thread
    start routine.
 */
void track_pthread_entry(lift_thread_metadata threads[NUMBER_THREAD_ENTRIES], void** call_site_ptr, void* entry_addr) {
    void* call_site = *call_site_ptr;
    for(int idx = 0; idx < NUMBER_THREAD_ENTRIES; idx++) {
        if(threads[idx].entry_addr == entry_addr && threads[idx].call_site == call_site) {
            return;
        }
        if(threads[idx].entry_addr == 0) {
            threads[idx].entry_addr = entry_addr;
            threads[idx].call_site = call_site;
            return;
        }
    }

    fprintf(stderr, "mclift: Exceeded maximum number of NUMBER_THREAD_ENTRIES!\n");
    exit(-1);
}

/*
    Emit instrumentation before call to pthread_create to capture the new thread to be spawned.
*/
int lift_pre_pthread_create_cb(mambo_context *ctx) {
    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
        exit(-1);
    }
#endif

    emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));
    emit_set_reg_ptr(ctx, x0, plugin_data->threads_entries);
    emit_set_reg(ctx, x1, (uintptr_t) &plugin_data->current_call_addr);
    // Correct address already in x2
    emit_safe_fcall(ctx, track_pthread_entry, 3);
    emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));
}

/*
    Emit instrumentation before call to GOMP_parallel to capture the new thread to be spawned.
*/
int lift_pre_gomp_parallel_cb(mambo_context *ctx) {
    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
        exit(-1);
    }
#endif

    emit_push(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));
    emit_mov(ctx, x2, x0);
    emit_set_reg_ptr(ctx, x0, plugin_data->threads_entries);
    emit_set_reg(ctx, x1, (uintptr_t) &plugin_data->current_call_addr);
    emit_safe_fcall(ctx, track_pthread_entry, 3);
    emit_pop(ctx, (1 << x0) | (1 << x1) | (1 << x2) | (1 << x3));
}

/*
    Emit instrumentation before call to __libc_start_main to capture the address of the main function.
*/
int lift_pre_libc_start_main(mambo_context *ctx) {
    // Get plugin data, so we can save the main address
    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_get_plugin_data(ctx);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't get the plugin data!\n");
        exit(-1);
    }
#endif
    // __libc_start_main takes an address to main as first argument, so to recover
    // address of the main function we have to save value of x0 right before the
    // call to __libc_start_main.
    emit_push(ctx, (1 << x0) | (1 << x1));
    emit_set_reg(ctx, x1, (uintptr_t) plugin_data);
    emit_a64_LDR_STR_unsigned_immed(ctx, 3, 0, 0, 0, x1, x0);
    emit_pop(ctx, (1 << x0) | (1 << x1));
}

/*
    Allocate global plugin data and register the plugin and its callbacks in MAMBO.
*/
__attribute__((constructor))
void init_lift() {
    mambo_context *ctx = mambo_register_plugin();

    assert(ctx != NULL);

#ifdef PERFORMANCE_MONITORING
    timers.dynamic_execution = get_virtual_counter();
#endif

    int ret;

    lift_plugin_data *plugin_data = (lift_plugin_data *) mambo_alloc(ctx, sizeof(lift_plugin_data));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data == NULL) {
        fprintf(stderr, "mclift: Couldn't allocate plugin data!\n");
        exit(-1);
    }
#endif

    plugin_data->cfg = (mambo_ht_t *) mambo_alloc(ctx, sizeof(mambo_ht_t));
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (plugin_data->cfg == NULL) {
        fprintf(stderr, "mclift: Couldn't allocate the hash map!\n");
        exit(-1);
    }
#endif

    ret = mambo_ht_init(plugin_data->cfg, 1 << 20, 0, 80, false);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Couldn't initialize the hash map!\n");
        exit(-1);
    }
#endif

    ret = pthread_mutex_init(&plugin_data->lock, NULL);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Couldn't initialize the pthread lock!\n");
        exit(-1);
    }
#endif

    for(int idx = 0; idx < NUMBER_THREAD_ENTRIES; idx++) {
        plugin_data->threads_entries[idx].entry_addr = NULL;
        plugin_data->threads_entries[idx].call_site = NULL;
    }

    ret = mambo_set_plugin_data(ctx, (void *) plugin_data);
#ifdef ALLOW_CRITICAL_PATH_CHECKS
    if (ret) {
        fprintf(stderr, "mclift: Couldn't set the plugin data!\n");
        exit(-1);
    }
#endif

    mambo_register_pre_thread_cb(ctx, &lift_pre_thread_cb);
    mambo_register_post_thread_cb(ctx, &lift_post_thread_cb);

    mambo_register_pre_inst_cb(ctx, &lift_pre_inst_cb);

    mambo_register_pre_basic_block_cb(ctx, &lift_pre_basic_block_cb);

    mambo_register_exit_cb(ctx, &lift_exit_cb);

#if defined(RECOVER_MAIN_ADDR_GLIBC)
    mambo_register_function_cb(ctx, "__libc_start_main", lift_pre_libc_start_main, NULL, 7);
#elif defined(LOAD_MAIN_ADDR)
    #error Loading the main address from the symbol table currently not implemented!
#else
    #error No method for the recovery of the main address has been selected!
#endif

#ifdef THREADS_SUPPORT
#ifdef PTHREADS_SUPPORT
    mambo_register_function_cb(ctx, "pthread_create", lift_pre_pthread_create_cb, NULL, 4);
#endif
#ifdef OPENMP_SUPPORT
    mambo_register_function_cb(ctx, "GOMP_parallel", lift_pre_gomp_parallel_cb, NULL, 4);
#endif
#endif
}

#endif
