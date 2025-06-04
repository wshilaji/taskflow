#include "test/test.h"

#include "absl/hash/hash.h"
#include "butil/logging.h"

void test_little() { LOG(INFO) << "hash int " << absl::Hash<int64_t>()(1234); }