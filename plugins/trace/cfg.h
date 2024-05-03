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

#include <stdint.h>

// DEFS

#define CFG_MAX_IN_NODES 1024

// ENUMS

typedef struct cfg_node cfg_node;
typedef struct cfg_edge cfg_edge;
typedef struct cfg_node_linked_list cfg_node_linked_list;

/// Type of the edge in the CFG
typedef enum {
    CFG_EDGE_NOTYPE, ///< Type of the edge not known yet or irrelevant
    CFG_TAKEN_BRANCH, ///< Edge followed when the branch condition is true
    CFG_SKIPPED_BRANCH ///< Edge followed when the branch condition is false
} cfg_edge_type;

/// Types of CFG nodes - one node can have multiple types, e.g., indirect function call
typedef enum {
    CFG_BASIC_BLOCK = 0x0, ///< Ends in unconditional branch
    CFG_CONDITIONAL_BLOCK = 0x1, ///< Ends in conditional branch
    CFG_FUNCTION_CALL = 0x8, ///< Ends in function call
    CFG_SVC = 0x10, ///< Ends in SVC
    CFG_RETURN = 0x20, ///< Ends in return statement
    CFG_INDIRECT_BLOCK = 0x40, ///< Ends in the indirect branch
    CFG_NATIVE_CALL = 0x80 ///< Ends in call to a library function that is not being lifted
} cfg_node_type;

/// Profile of the node obtained from MAMBO tracing
typedef enum {
    CFG_NODE_COLD = 0, ///< Node executed less than 256 times
    CFG_NODE_HOT = 1, ///< Node executed more than 256 times
    CFG_NODE_HOT_HEAD = 2 ///< Node executed more than 256 times and it is a first block of the hot section
} cfg_node_profile;

// STRUCTS

/// Edge in the CFG. NOTE: Before modifying see instrumentation.S
struct cfg_edge{
    cfg_node* node; ///< NOTE: Has to be the first field for the instrumentation to work correctly.
    cfg_edge* next;

    cfg_edge_type type;
};

/// Node in the CFG
struct cfg_node {
    void* start_addr; ///< Start address of the node in the original binary
    void* end_addr; ///< End address of the node in the original binary

    cfg_edge* edges; ///< Out edges of the node

    cfg_node_type type; ///< Type of the node

    uint64_t order_id; ///< Defines order in which basic blocks were first executed

    uint32_t branch_reg; ///< Register used for jumping by the indirect branch

    cfg_node_profile profile; ///< Profile of the node - tells if nodes executed more than 256 times
};

/// Linked list to store multiple nodes
struct cfg_node_linked_list {
    cfg_node* node;
    cfg_node_linked_list* next;
};

// FUNCTIONS

void initialize_node(cfg_node* node);

void initialize_edge(cfg_edge* edge, cfg_edge_type type);
