
#include <iostream>

#include "ecm/containers/hash_map.h"
#include "test/test.h"

void test_containers() {
  ecm::HashMap<int64_t, int64_t> m;
  m.Init(1000);
  int64_t v = 0;

  check_true(m.Put(1, 1) == 0);
  check_true(m.Get(&v, 1));
  check_true(v == 1);

  // conflict
  check_true(m.Put(1025, 2) == 0);
  check_true(m.Get(&v, 1));
  check_true(v == 1);
  check_true(m.Get(&v, 1025));
  check_true(v == 2);

  check_true(m.Put(1234567, 100) == 0);
  check_true(m.Get(&v, 1234567));
  check_true(v == 100);

  m.ForEach([](auto k, auto v) {
    std::cout << k << " -> " << v << std::endl;
    return true;
  });

  check_true(m.Put(1, 1000) == 1);
  check_true(m.Get(&v, 1));
  check_true(v == 1000);

  // conflict
  check_true(m.Put(1025, 2000) == 2);
  check_true(m.Get(&v, 1));
  check_true(v == 1000);
  check_true(m.Get(&v, 1025));
  check_true(v == 2000);

  m.ForEach([](auto k, auto v) {
    std::cout << k << " -> " << v << std::endl;
    return true;
  });
}