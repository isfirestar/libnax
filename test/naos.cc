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

class CIllegalInetAddressCheck {
    std::string str_ipaddr_;

public:
    CIllegalInetAddressCheck(const char *ipstr) : str_ipaddr_(ipstr) {
        ;
    }

    std::string GetInetAddr() const {
        return str_ipaddr_;
    }
};

class TestSuiteIllegalInetAddr : public ::testing::TestWithParam<CIllegalInetAddressCheck> {
    
};

INSTANTIATE_TEST_SUITE_P(instance, TestSuiteIllegalInetAddr, ::testing::Values(
    CIllegalInetAddressCheck(""),
    CIllegalInetAddressCheck("1"),
    CIllegalInetAddressCheck("255.255.255.255."),
    CIllegalInetAddressCheck(".255.255.255.255"),
    CIllegalInetAddressCheck("255.255.255.2a5"),
    CIllegalInetAddressCheck("256.0.0.1"),
    CIllegalInetAddressCheck("255.256.0.1"),
    CIllegalInetAddressCheck("255.255.256.1"),
    CIllegalInetAddressCheck("255.255.255.256"),
    CIllegalInetAddressCheck("-1.0.0.3"),
    CIllegalInetAddressCheck("192.168.0.-1"),
    CIllegalInetAddressCheck("1.2.3.4.5"),
    CIllegalInetAddressCheck("192..168.0.1"),
    CIllegalInetAddressCheck("192.168..0.1"),
    CIllegalInetAddressCheck("32-7.5.4")
));

TEST_P(TestSuiteIllegalInetAddr, IllegalIpv4Check)
{
    CIllegalInetAddressCheck arg = GetParam();

    nsp_boolean_t successful = naos_is_legal_ipv4(arg.GetInetAddr().c_str());
    EXPECT_EQ(successful, nsp_false);
}

class CIpv4ToUCheck {
    std::string str_ipaddr_;
    uint32_t u32_ipaddr_;
    enum byte_order_t byte_order_;

public:
    CIpv4ToUCheck(const char *ipstr, uint32_t ipaddr, enum byte_order_t byte_order) : str_ipaddr_(ipstr), u32_ipaddr_(ipaddr), byte_order_(byte_order) {
        ;
    }

    std::string GetInetAddr() const {
        return str_ipaddr_;
    }

    uint32_t GetU32InetAddr() const {
        return u32_ipaddr_;
    }

    enum byte_order_t GetByteOrder() const {
        return byte_order_;
    }
};

class TestSuiteIpv4ToU : public ::testing::TestWithParam<CIpv4ToUCheck> {
    
};

INSTANTIATE_TEST_SUITE_P(instanceTestIpv4ToU, TestSuiteIpv4ToU, ::testing::Values(
    CIpv4ToUCheck("222.173.215.69", 0xdeadd745, kByteOrder_LittleEndian),
    CIpv4ToUCheck("222.173.215.69", 0x45d7adde, kByteOrder_BigEndian),
    CIpv4ToUCheck(" 222.173.215.69", 0, kByteOrder_LittleEndian),
    CIpv4ToUCheck("222. 173.215.69", 0, kByteOrder_BigEndian),
    CIpv4ToUCheck("222.173. 215.69", 0, kByteOrder_LittleEndian),
    CIpv4ToUCheck("222.173.215. 69", 0, kByteOrder_BigEndian),
    CIpv4ToUCheck("222.173.215.69 ", 0, kByteOrder_LittleEndian),
    CIpv4ToUCheck("262.173.215.69", 0, kByteOrder_BigEndian)
));

TEST_P(TestSuiteIpv4ToU, Ipv4ToU)
{
    CIpv4ToUCheck arg = GetParam();

    uint32_t value = naos_ipv4tou(arg.GetInetAddr().c_str(), arg.GetByteOrder());
    EXPECT_EQ(value, arg.GetU32InetAddr());
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
