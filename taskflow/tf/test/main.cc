
void abyss_test();
void taskflow_test();
void test_config_center();
void test_lancer();
void test_metrics();
void test_little();
void test_containers();
void test_core();

int main(int argc, char *argv[]) {
  abyss_test();
  taskflow_test();
  test_config_center();
  test_lancer();
  test_metrics();
  test_little();
  test_containers();
  test_core();

  return 0;
}
