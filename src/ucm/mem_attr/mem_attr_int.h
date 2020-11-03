/**
 * Copyright (C) NVIDIA Corporation. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCM_MEM_ATTR_INT_H_
#define UCM_MEM_ATTR_INT_H_


#include "mem_attr.h"

#include <ucs/memory/memory_type.h>
#include <ucs/type/status.h>


typedef struct ucm_mem_attr {
    ucs_memory_type_t mem_type;
    int (*cmp)(ucm_mem_attr_h mem_attr1, ucm_mem_attr_h mem_attr2);
    void (*destroy)(ucm_mem_attr_h mem_attr);
} ucm_mem_attr_t;


#endif
