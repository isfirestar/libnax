#include "etc_reader.h"

#include <gtest/gtest.h>

TEST(DoTestEtcReader, TestEtcReader)
{
    nsp_status_t status;
    objhld_t hld;
    static const char test_buffer[] = "key1=value1\n"
            "key2=value2\n"
            " key3= value3\r\n"
            " key4 =value4 \n"
            "#key5 = value5\n"
            " # key6 = value6\n"
            "key7 $= value5\n"
            "key8 = value8  # these are some comment\n";
    const char *value;

    status = etcr_load_from_memory(test_buffer, strlen(test_buffer), &hld);
    ASSERT_TRUE(NSP_SUCCESS(status));
    status = etcr_query_value_bykey(hld, "key1", &value);
    ASSERT_TRUE( (NSP_SUCCESS(status) && 0 == strcmp(value, "value1")) );
    status = etcr_query_value_bykey(hld, "key2", &value);
    ASSERT_TRUE( (NSP_SUCCESS(status) && 0 == strcmp(value, "value2")) );
    status = etcr_query_value_bykey(hld, "key3", &value);
    ASSERT_TRUE( (NSP_SUCCESS(status) && 0 == strcmp(value, "value3")) );
    status = etcr_query_value_bykey(hld, "key4", &value);
    ASSERT_TRUE( (NSP_SUCCESS(status) && 0 == strcmp(value, "value4")) );
    status = etcr_query_value_bykey(hld, "key5", &value);
    ASSERT_TRUE( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key6", &value);
    ASSERT_TRUE( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key7", &value);
    ASSERT_TRUE( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key8", &value);
    ASSERT_TRUE( (NSP_SUCCESS(status) && 0 == strcmp(value, "value8")) );

    etcr_unload(hld);
}
