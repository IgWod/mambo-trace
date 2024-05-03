/*
  Copyright 2024 Igor Wodiany
  Copyright 2024 The University of Manchester

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this trace except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "save.h"

void save(mambo_context* ctx, mambo_ht_t* cfg, void* main_addr, lift_thread_metadata threads[NUMBER_THREAD_ENTRIES]) {
    time_t timestamp = time(NULL);
    char tracename[128];

    sprintf(tracename, "%ld.mtrace", (long) timestamp);

    FILE *trace = fopen(tracename, "wb");

    uintptr_t main_relative_addr = (uintptr_t) main_addr - global_data.base_addr;
    fwrite(&main_relative_addr, sizeof(main_addr), 1, trace);

    for (int index = 0; index < cfg->size; index++) {
        if (cfg->entries[index].key != 0) {
            cfg_node *node = (cfg_node *) cfg->entries[index].value;

            int64_t begin_node = -1;

            fwrite(&begin_node, sizeof(int64_t), 1, trace);

            uintptr_t start_addr = (uintptr_t) node->start_addr - global_data.base_addr;
            fwrite(&start_addr, sizeof(node->start_addr), 1, trace);
            uintptr_t end_addr = (uintptr_t) node->end_addr - global_data.base_addr;
            fwrite(&end_addr, sizeof(node->end_addr), 1, trace);
            fwrite(&node->branch_reg, sizeof(node->branch_reg), 1, trace);
            fwrite(&node->type, sizeof(node->type), 1, trace);

            cfg_edge* edge = node->edges;
            while(edge != NULL) {
                if(edge->node != NULL) {
                    uintptr_t edge_addr = (uintptr_t) edge->node - global_data.base_addr;
                    fwrite(&edge_addr, sizeof(edge->node), 1, trace);
                    fwrite(&edge->type, sizeof(edge->type), 1, trace);
                }
                edge = edge->next;
            }
        }
    }

    // TODO: Save thread information to the file.

    fclose(trace);
}
