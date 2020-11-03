/**
 * Copyright (C) NVIDIA Corporation. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCM_MEM_ATTR_H_
#define UCM_MEM_ATTR_H_

#include <ucs/memory/memory_type.h>
#include <ucs/type/status.h>
#include <stddef.h>

typedef struct ucm_mem_attr *ucm_mem_attr_h;

ucs_status_t ucm_mem_attr_get(const void *address, size_t length,
                              ucm_mem_attr_h *mem_attr_p);
ucs_memory_type_t ucm_mem_attr_get_type(ucm_mem_attr_h mem_attr);
int ucm_mem_attr_cmp(ucm_mem_attr_h mem_attr1, ucm_mem_attr_h mem_attr2);
void ucm_mem_attr_destroy(ucm_mem_attr_h mem_attr);

#endif
