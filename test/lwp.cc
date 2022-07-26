// g++ threading.cc -I ../include -lgtest -L ./ -lnax -pthread -othreading -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>

#include "threading.h"
#include "ifos.h"

class CThreadingTester : public testing::Test
{
protected:
    lwp_t *m_handle = nullptr;
    nsp_status_t thret_status;
    static void *threadingRoutine(void *p);

    virtual void SetUp() override final;
    virtual void TearDown() override final;
};

void *CThreadingTester::threadingRoutine(void *p)
{
    CThreadingTester *pthis = (CThreadingTester *)p;
    EXPECT_TRUE((pthis != nullptr));

    if (pthis->m_handle) {
        // pthread_self
        EXPECT_EQ(pthis->m_handle->pid, lwp_self());

        // yield
        nsp_status_t status = lwp_yield(pthis->m_handle);
        EXPECT_TRUE(NSP_SUCCESS(status));

        // affinity
     }

    // sleep 1 second
    lwp_delay(1 * 1000 * 1000);
    pthis->thret_status = 1024;
    return &pthis->thret_status;
}

void CThreadingTester::SetUp()
{

}

void CThreadingTester::TearDown()
{

}

TEST_F(CThreadingTester, CThreadingNormalTest)
{
    nsp_status_t status;
    lwp_t lwp;

    status = lwp_create(NULL, 0, CThreadingTester::threadingRoutine, NULL);
    EXPECT_FALSE(NSP_SUCCESS(status));
    status = lwp_create(&lwp, 0, NULL, NULL);
    EXPECT_FALSE(NSP_SUCCESS(status));
    status = lwp_create(&lwp, 0, CThreadingTester::threadingRoutine, this);
    m_handle = &lwp;
    EXPECT_TRUE(NSP_SUCCESS(status));

    nsp_boolean_t isJoinable = lwp_joinable(&lwp);
    EXPECT_EQ(__true__, isJoinable);

    void *retval;
    status = lwp_join(NULL, &retval);
    EXPECT_FALSE(NSP_SUCCESS(status));
    status = lwp_join(&lwp, &retval);
    EXPECT_TRUE(NSP_SUCCESS(status));
    EXPECT_EQ( *(nsp_status_t *)retval, 1024);
    EXPECT_EQ(retval, &this->thret_status);

    // this thread is detached
    isJoinable = lwp_joinable(&lwp);
    EXPECT_EQ(__false__, isJoinable);
    status = lwp_join(&lwp, &retval);
    EXPECT_FALSE(NSP_SUCCESS(status));
}

TEST_F(CThreadingTester, CThreadingDetachTest)
{
    nsp_status_t status;
    lwp_t lwp;

    status = lwp_create(NULL, 0, CThreadingTester::threadingRoutine, NULL);
    EXPECT_FALSE(NSP_SUCCESS(status));
    status = lwp_create(&lwp, 0, NULL, NULL);
    EXPECT_FALSE(NSP_SUCCESS(status));
    m_handle = nullptr;
    status = lwp_create(&lwp, 0, CThreadingTester::threadingRoutine, this);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // detach the thread
    status = lwp_detach(&lwp);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = lwp_detach(&lwp);
    EXPECT_FALSE(NSP_SUCCESS(status));
    nsp_boolean_t isJoinable = lwp_joinable(&lwp);
    EXPECT_EQ(__false__, isJoinable);

    // can not join
    status = lwp_join(&lwp, NULL);
    EXPECT_FALSE(NSP_SUCCESS(status));
}

TEST_F(CThreadingTester, CThreadingComplexTest)
{
    nsp_status_t status;
    lwp_t lwp;

    status = lwp_create(&lwp, 0, CThreadingTester::threadingRoutine, this);
    m_handle = &lwp;
    EXPECT_TRUE(NSP_SUCCESS(status));

    abuff_pthread_name_t name;
    abuff_strcpy(&name, "gtest-pthread-name");
    status = lwp_setname(&lwp, &name);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = lwp_getname(&lwp, &name);
    EXPECT_TRUE(NSP_SUCCESS(status));
    EXPECT_EQ(0, strcmp(name.u.cst, "gtest-pthread-n"));

    status = lwp_setkey(&lwp, this);
    EXPECT_TRUE(NSP_SUCCESS(status));
    void *p = lwp_getkey(&lwp);
    EXPECT_TRUE(p != nullptr);
    EXPECT_TRUE((CThreadingTester *)p == this);

    status = lwp_join(&lwp, NULL);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = lwp_setname(&lwp, &name);
    EXPECT_EQ(status, -EINVAL);
    status = lwp_getname(&lwp, &name);
    EXPECT_EQ(status, -EINVAL);
}

