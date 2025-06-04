#include "butil/fast_rand.h"
#include "butil/logging.h"
#include "ecm/core/file_system.h"
#include "ecm/taskflow/bthread/bthread_notification.h"
#include "ecm/taskflow/bthread/bthread_schedule.h"
#include "ecm/taskflow/executor.h"

#include "test.h"

using namespace ecm;
using namespace taskflow;

struct InType : public Data {
  InType() : Data("InType") {}

  int x = 0;
};

struct IncOp : public OpKernel {
public:
  IncOp(OpKernelConstruction *ctx)
      : OpKernel(ctx, {{"InType", "in"}}, {{"InType", "out"}}) {}

  Status Compute(OpKernelContext *ctx) override {
    const InType *input;
    ECM_RETURN_IF_ERROR(GetInput(ctx, "in", &input));

    InType *output;
    ECM_RETURN_IF_ERROR(AllocOutput(ctx, "out", &output));

    output->x = input->x + 1;

    return OkStatus();
  }
};

struct AddOp : public OpKernel {
public:
  AddOp(OpKernelConstruction *ctx)
      : OpKernel(ctx, {{"InType", "in_a"}, {"InType", "in_b"}},
                 {{"InType", "out"}}) {}

  Status Compute(OpKernelContext *ctx) override {
    const InType *in_a, *in_b;
    ECM_RETURN_IF_ERROR(GetInput(ctx, "in_a", &in_a));
    ECM_RETURN_IF_ERROR(GetInput(ctx, "in_b", &in_b));

    InType *output;
    ECM_RETURN_IF_ERROR(AllocOutput(ctx, "out", &output));

    output->x = in_a->x + in_b->x;

    return OkStatus();
  }
};

ECM_REGISTER_KERNEL("IncOp", IncOp);
ECM_REGISTER_KERNEL("AddOp", AddOp);

void taskflow_test() {
  std::string fname = "test/data/graph.json";
  GraphProto graph_pb;
  check_status(
      ecm::ReadJsonToProto(ecm::FileSystem::Default(), fname, &graph_pb));
  std::cout << graph_pb.DebugString() << std::endl;

  auto graph = std::make_unique<Graph>();
  check_status(graph->Build(OpRegistry::Default(), graph_pb));

  graph->DebugPrint();

  Executor executor(graph.release(), new BThreadSchedule,
                    new BThreadNotification);

  Session session;

  auto input = std::make_unique<InType>();
  input->x = 100;

  session.SetInput("$input:input", std::move(input));
  session.SetOutputs({"output"});

  check_status(executor.Execute(&session));

  InType *output;
  check_status(session.GetOutput("output:out", &output));
  LOG(INFO) << output->x;
}