#pragma once

#include <vector>

namespace ecm {

// rand generate k indices in range [0, n)
void fast_rand_select_k(std::vector<int> *indices, int n, int k);

} // namespace ecm