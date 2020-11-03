/**
 * Copyright (C) NVIDIA Corporation. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "mem_attr.h"
#include "mem_attr_int.h"

#include <ucs/memory/memory_type.h>
#include <ucm/event/event.h>


static int ucm_mem_attr_cmp_type(ucm_mem_attr_h mem_attr1,
                                 ucm_mem_attr_h mem_attr2)
{
    if (mem_attr1->mem_type == mem_attr2->mem_type) return 0;
    return 1;
}

static void ucm_mem_attr_destroy_host(ucm_mem_attr_h mem_attr)
{
    /* Nothing to be done for host memory attributes */
}

/* All host memory will have the same attributes (only a type).
 * So, they will all point to this static struct */
static ucm_mem_attr_t mem_attr_host = {
    .mem_type = UCS_MEMORY_TYPE_HOST,
    .cmp      = &ucm_mem_attr_cmp_type,
    .destroy  = &ucm_mem_attr_destroy_host
};

ucs_status_t ucm_mem_attr_get(const void *address, size_t length,
                              ucm_mem_attr_h *mem_attr_p)
{
    ucm_event_installer_t *event_installer;
    ucs_status_t status;
    int failure = 0;

    ucs_list_for_each(event_installer, &ucm_event_installer_list, list) {
        if (event_installer->get_mem_attr == NULL) continue;
        status = event_installer->get_mem_attr(address, length, mem_attr_p);
        if (status == UCS_OK) return UCS_OK;
        if (status != UCS_ERR_INVALID_ADDR) failure = 1;
    }

    if (failure) return UCS_ERR_NO_RESOURCE;

    /* none of the installers recognized the address. So, it must be HOST */
    *mem_attr_p = &mem_attr_host;
    return UCS_OK;
}

ucs_memory_type_t ucm_mem_attr_get_type(ucm_mem_attr_h mem_attr)
{
    return mem_attr->mem_type;
}

int ucm_mem_attr_cmp(ucm_mem_attr_h mem_attr1, ucm_mem_attr_h mem_attr2)
{
    if (ucm_mem_attr_cmp_type(mem_attr1, mem_attr2)) return 1;
    return mem_attr1->cmp(mem_attr1, mem_attr2);
}

void ucm_mem_attr_destroy(ucm_mem_attr_h mem_attr)
{
    if (mem_attr) mem_attr->destroy(mem_attr);
}
