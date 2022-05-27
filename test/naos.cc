// g++ naos.cc -I ../include -lgtest -L ./ -lnax -pthread -onaos -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>
#include <string>

#include "naos.h"
#include "ifos.h"

TEST(DoTestByteOrderChange, ByteOrderChange)
{
    uint32_t dword = (uint32_t)ifos_random(0,0);
    EXPECT_NE(0, dword);
    uint32_t changed_dw = naos_chord32(dword);
    EXPECT_EQ((changed_dw & 0xff), ((dword >> 24) & 0xff));
    EXPECT_EQ(((changed_dw >> 8) & 0xff), ((dword >> 16) & 0xff));
    EXPECT_EQ(((changed_dw >> 16) & 0xff), ((dword >> 8) & 0xff));
    EXPECT_EQ(((changed_dw >> 24) & 0xff), (dword & 0xff));

    uint16_t word = (uint32_t)ifos_random(0,0) & 0xffff;
    EXPECT_NE(0, word);
    uint32_t changed_w = naos_chord16(word);
    EXPECT_EQ((changed_w & 0xff), ((word >> 8) & 0xff));
    EXPECT_EQ(((changed_w >> 8) & 0xff), (word & 0xff));
}

TEST(DoIpv4Check, Ipv4Check)
{
    nsp_boolean_t successful;

    successful = naos_is_legal_ipv4("");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("255.255.255.255.");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4(".255.255.255.255");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("255.255.255.2a5");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("256.0.0.1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("255.256.0.1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("255.255.256.1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("255.255.255.256");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("-1.0.0.3");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("192.168.0.-1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("1.2.3.4.5");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("192..168.0.1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("192.168..0.1");
    EXPECT_EQ(successful, nsp_false);
    successful = naos_is_legal_ipv4("32-7.5.4");
    EXPECT_EQ(successful, nsp_false);
}

TEST(DoIpv4ToU, Ipv4ToU)
{
    uint32_t value;

    value = naos_ipv4tou("222.173.215.69", kByteOrder_LittleEndian);
    EXPECT_EQ(value, 0xdeadd745);
    value = naos_ipv4tou("222.173.215.69", kByteOrder_BigEndian);
    EXPECT_EQ(value, 0x45d7adde);
    value = naos_ipv4tou(" 222.173.215.69", kByteOrder_LittleEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("222. 173.215.69", kByteOrder_BigEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("222.173. 215.69", kByteOrder_LittleEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("222.173.215. 69", kByteOrder_BigEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("222.173.215.69 ", kByteOrder_LittleEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("262.173.215.69", kByteOrder_BigEndian);
    EXPECT_EQ(value, 0);
    value = naos_ipv4tou("222.173.215.69", 4);
    EXPECT_EQ(value, 0);
}

TEST(DpIpv4ToS, Ipv4ToS)
{
    abuff_naos_inet_t inetstr;
    nsp_status_t status;

    status = naos_ipv4tos(0xdeadd745, NULL);
    EXPECT_EQ(status, -EINVAL);
    status = naos_ipv4tos(0xdeadd745, &inetstr);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_EQ(0, strcmp(inetstr.u.cst, "222.173.215.69"));
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
