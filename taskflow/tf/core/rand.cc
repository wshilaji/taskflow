#include "core/rand.h"
#include "butil/fast_rand.h"
#include <numeric>

namespace ecm {

void fast_rand_select_k(std::vector<int> *indices, int n, int k) {
  auto len = n > k ? k : n;
  indices->resize(len);
  std::iota(indices->begin(), indices->end(), 0);

  for (int i = k, j; i < n; ++i) {
    j = butil::fast_rand_less_than(i + 1);
    if (j < k) {
      indices->at(j) = i;
    }
  }
}

} // namespace ecm