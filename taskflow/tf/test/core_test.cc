#include <iostream>

#include "ecm/core/env.h"
#include "ecm/core/memory.h"
#include "test/test.h"

void test_core() {

  ecm::MemoryInfo info;
  check_status(ecm::GetMemoryInfo(&info));

  std::cout << "vm rss: " << info.vm_rss << std::endl;
}
