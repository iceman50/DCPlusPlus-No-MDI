#include "testbase.h"

#include <atomic>

using namespace dcpp;

TEST(testlockfree, test_atomic)
{
	ASSERT_TRUE(std::atomic_bool().is_lock_free());
	ASSERT_TRUE(std::atomic<int32_t>().is_lock_free());
	ASSERT_TRUE(std::atomic<uint32_t>().is_lock_free());
	ASSERT_TRUE(std::atomic<int64_t>().is_lock_free());
	ASSERT_TRUE(std::atomic<uint64_t>().is_lock_free());
}
