/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

enum intel_pagefault_access {
   /** Faults caused by read access */
   INTEL_PAGEFAULT_ACCESS_READ,

   /** Faults caused by write access */
   INTEL_PAGEFAULT_ACCESS_WRITE,

   /** Faults caused by atomic access */
   INTEL_PAGEFAULT_ACCESS_ATOMIC,
};

enum intel_pagefault_type {
   /** The page or the page table was not present */
   INTEL_PAGEFAULT_TYPE_NOT_PRESENT,

   /** The page was present, but was not writable */
   INTEL_PAGEFAULT_TYPE_WRITE_ACCESS,

   /** The page was present, but was not atomic-able */
   INTEL_PAGEFAULT_TYPE_ATOMIC_ACCESS,
};

enum intel_pagefault_level {
   /** GraphicsAddress[20:12] - Page Table Entry */
   INTEL_PAGEFAULT_LEVEL_PTE,

   /** GraphicsAddress[29:21] - Page Directory Entry */
   INTEL_PAGEFAULT_LEVEL_PDE,

   /** GraphicsAddress[38:30] - Page Directory Pointer */
   INTEL_PAGEFAULT_LEVEL_PDP,

   /** GraphicsAddress[47:39] - Page Map Level 4 */
   INTEL_PAGEFAULT_LEVEL_PML4,

   /** GraphicsAddress[56:48] - Page Map Level 5 */
   INTEL_PAGEFAULT_LEVEL_PML5,
};

struct intel_pagefault_info {
    uint64_t address;
    uint32_t precision;
    enum intel_pagefault_access access;
    enum intel_pagefault_type type;
    enum intel_pagefault_level level;
};

struct intel_pagefault_buffer {
   unsigned size;
   struct intel_pagefault_info items[];
};

const char *
intel_pagefault_access_to_string(enum intel_pagefault_access access);
const char *
intel_pagefault_type_to_string(enum intel_pagefault_type type);
const char *
intel_pagefault_level_to_string(enum intel_pagefault_level level);
