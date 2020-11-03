/**
 * Copyright (C) NVIDIA Corporation. 2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <common/test.h>
#include <common/mem_buffer.h>
extern "C" {
#include <ucm/mem_attr/mem_attr.h>
#include <ucm/mem_attr/mem_attr_int.h>
}


class test_mem_attr : public ucs::test_with_param<ucs_memory_type_t> {
protected:

    virtual void init() {
        ucs::test_with_param<ucs_memory_type_t>::init();
    }

    virtual void cleanup() {
        ucs::test_with_param<ucs_memory_type_t>::cleanup();
    }

    ucm_mem_attr_h get_mem_attr(void *address, size_t length) {
        ucs_status_t status;
        ucm_mem_attr_h mem_attr = NULL;
        status = ucm_mem_attr_get(address, length, &mem_attr);
        ASSERT_UCS_OK(status);
        EXPECT_TRUE(mem_attr != NULL);
        return mem_attr;
    }

    static const size_t size = 1024;
};

UCS_MT_TEST_P(test_mem_attr, basic, 10) {
    mem_buffer buf(size, GetParam());
    ucm_mem_attr_h mem_attr = get_mem_attr(buf.ptr(), buf.size());
    ASSERT_EQ(GetParam(), mem_attr->mem_type);
    ucm_mem_attr_destroy(mem_attr);
}

UCS_MT_TEST_P(test_mem_attr, get_type, 10) {
    mem_buffer buf(size, GetParam());
    ucm_mem_attr_h mem_attr = get_mem_attr(buf.ptr(), buf.size());
    EXPECT_EQ(GetParam(), ucm_mem_attr_get_type(mem_attr));
    ucm_mem_attr_destroy(mem_attr);
}

UCS_MT_TEST_P(test_mem_attr, destroy, 10) {
    mem_buffer buf1(size, GetParam());
    mem_buffer buf2(size, GetParam());

    ucm_mem_attr_h mem_attr1 = get_mem_attr(buf1.ptr(), buf1.size());
    ucm_mem_attr_h mem_attr2 = get_mem_attr(buf2.ptr(), buf2.size());

    EXPECT_EQ(GetParam(), ucm_mem_attr_get_type(mem_attr1));
    EXPECT_EQ(GetParam(), ucm_mem_attr_get_type(mem_attr2));

    ucm_mem_attr_destroy(mem_attr1);
    EXPECT_EQ(GetParam(), ucm_mem_attr_get_type(mem_attr2));
    ucm_mem_attr_destroy(mem_attr2);
}

UCS_MT_TEST_P(test_mem_attr, cmp_same_buf, 10) {
    mem_buffer buf(size, GetParam());

    ucm_mem_attr_h mem_attr1 = get_mem_attr(buf.ptr(), buf.size());
    ucm_mem_attr_h mem_attr2 = get_mem_attr(buf.ptr(), buf.size());

    EXPECT_EQ(0, ucm_mem_attr_cmp(mem_attr1, mem_attr2));

    ucm_mem_attr_destroy(mem_attr1);
    ucm_mem_attr_destroy(mem_attr2);
}

UCS_TEST_SKIP_COND_P(test_mem_attr, cmp_non_host,
                     GetParam() == UCS_MEMORY_TYPE_HOST) {
    /* Any two non-host allocations will have different attributes */
    mem_buffer buf1(size, GetParam());
    mem_buffer buf2(size, GetParam());

    ucm_mem_attr_h mem_attr1 = get_mem_attr(buf1.ptr(), buf1.size());
    ucm_mem_attr_h mem_attr2 = get_mem_attr(buf2.ptr(), buf2.size());

    EXPECT_NE(0, ucm_mem_attr_cmp(mem_attr1, mem_attr2));

    ucm_mem_attr_destroy(mem_attr1);
    ucm_mem_attr_destroy(mem_attr2);
}

UCS_TEST_SKIP_COND_P(test_mem_attr, cmp_non_host_release,
                     GetParam() == UCS_MEMORY_TYPE_HOST) {
    ucm_mem_attr_h prev_attr;

    {
        mem_buffer buf(size, GetParam());
        prev_attr = get_mem_attr(buf.ptr(), buf.size());
    }

    for (int i = 0; i < 10; i++) {
        mem_buffer buf(size, GetParam());
        ucm_mem_attr_h mem_attr = get_mem_attr(buf.ptr(), buf.size());
        EXPECT_NE(0, ucm_mem_attr_cmp(mem_attr, prev_attr));
        ucm_mem_attr_destroy(prev_attr);
        prev_attr = mem_attr;
    }

    ucm_mem_attr_destroy(prev_attr);
}


UCS_MT_TEST_F(test_mem_attr, cmp_host, 10) {
    /* All host allocations will have the same attributes */
    mem_buffer buf1(size, UCS_MEMORY_TYPE_HOST);
    mem_buffer buf2(size, UCS_MEMORY_TYPE_HOST);

    ucm_mem_attr_h mem_attr1 = get_mem_attr(buf1.ptr(), buf1.size());
    ucm_mem_attr_h mem_attr2 = get_mem_attr(buf2.ptr(), buf2.size());

    EXPECT_EQ(0, ucm_mem_attr_cmp(mem_attr1, mem_attr2));

    ucm_mem_attr_destroy(mem_attr1);
    ucm_mem_attr_destroy(mem_attr2);
}

UCS_MT_TEST_F(test_mem_attr, cmp_diff_types, 10) {
    /* Any two allocations of different memory
     * types will have different attributes.
     */
    const std::vector<ucs_memory_type_t>& mem_types =
        mem_buffer::supported_mem_types();
    for (size_t i = 0; i < mem_types.size(); i++) {
            mem_buffer buf1(size, mem_types[i]);
            ucm_mem_attr_h mem_attr1 = get_mem_attr(buf1.ptr(), buf1.size());
        for (size_t j = i + 1; j < mem_types.size(); j++) {
            mem_buffer buf2(size, mem_types[j]);
            ucm_mem_attr_h mem_attr2 = get_mem_attr(buf2.ptr(), buf2.size());
            EXPECT_NE(0, ucm_mem_attr_cmp(mem_attr1, mem_attr2));
            ucm_mem_attr_destroy(mem_attr2);
        }
        ucm_mem_attr_destroy(mem_attr1);
    }
}

INSTANTIATE_TEST_CASE_P(mem_type, test_mem_attr,
                        ::testing::ValuesIn(mem_buffer::supported_mem_types()));
