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

/*
    Function for tracing targets of indirect branches. This function obeys standard ARM64 Linux ELF ABI. It assumes that
    the maximum number of indirect branches is 4096 (0xfff mask) and that the *node* field is on top of the cfg_edge
    structure. NOTE: Any changes to the cfg_edge structure or NUMBER_INDIRECT_TARGETS may break this routine.
*/

.global track_branch_target
.func track_branch_target
.type track_branch_target, %function

track_branch_target:
        and    x8, x0, #0xfff
        add    x8, x1, x8, lsl #5
        ldr    x9, [x8]
        cmp    x9, x0
        b.eq   track_branch_target.exists
        cbz    x9, track_branch_target.add
        mov    x9, x0
track_branch_target.loop:
        add    w8, w9, #1
        and    x9, x8, #0xfff
        add    x8, x1, x9, lsl #5
        ldr    x10, [x8]
        cbz    x10, track_branch_target.add
        cmp    x10, x0
        b.ne   track_branch_target.loop
track_branch_target.exists:
        ret
track_branch_target.add:
        str    x0, [x8]
        ret

.endfunc
