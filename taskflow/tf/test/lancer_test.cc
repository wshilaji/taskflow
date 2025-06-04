#include "ecm/core/file_system.h"
#include "ecm/log/lancer.h"
#include "test/test.h"

void test_lancer() {
  ecm::LancerConfig conf;
  auto w_conf = conf.add_writer();
  w_conf->set_name("test");
  w_conf->set_log_id("000000");
}