#include "ecm/metrics/metrics.h"
#include "test/test.h"

ecm::Status test_check() {
  ECM_RETURN_CHECK(1 == 2);
  return ecm::OkStatus();
}

void test_metrics() {
  ecm::TransactionRecorder latency_recorder("rank", {"model_name"});
  auto s = latency_recorder.Capture(
      {"model1"}, []() -> ecm::Status { return ecm::OkStatus(); });
  check_status(s);

  LOG(INFO) << test_check();
}