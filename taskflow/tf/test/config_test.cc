#include "ecm/config/paladin.h"
#include "test/configuration_strap.pb.h"
#include "test/test.h"

char configuration_strap[] = "configuration_strap";

void test_config_center() {

  setenv("CITADEL_CONF_PATH", "test/citadel-config", 1);
  setenv("APP_ID", "test", 1);
  setenv("POD_IP", "1.1.1.1", 1);

  ecm::Paladin paladin;
  check_status(paladin.Init("test/paladin_config"));
}