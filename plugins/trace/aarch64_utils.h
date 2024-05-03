/*
  Copyright 2024 Igor Wodiany
  Copyright 2024 The Univesrity of Manchester

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

// FUNCTIONS

/**
 * Read value of the virtual CPU counter at user level (EL0).
 *
 * @return Current value of the counter
 */
uint64_t get_virtual_counter(void);

/**
 * Read frequency of the virtual CPU counter at user level (EL0).
 *
 * @return Current frequency of the counter
 */
uint64_t get_virtual_counter_frequency(void);
