#include "ecm/taskflow/loader/loader.h"
#include "ecm/core/file_system.h"
#include "ecm/taskflow/bthread/bthread_notification.h"
#include "ecm/taskflow/bthread/bthread_schedule.h"

namespace ecm::taskflow {

Status Loader::Init(const std::string &graph_path) {

  std::vector<std::string> graph_files;
  ECM_RETURN_IF_ERROR(
      FileSystem::Default()->GetChildren(graph_path, &graph_files));

  for (auto &graph_file : graph_files) {
    auto graph_file_path = format("{}/{}", graph_path, graph_file);
    GraphProto pb_graph;
    ECM_RETURN_IF_ERROR(
        ReadJsonToProto(FileSystem::Default(), graph_file_path, &pb_graph));

    auto graph = std::make_unique<Graph>();
    ECM_RETURN_IF_ERROR(graph->Build(OpRegistry::Default(), pb_graph));

    auto executor = std::make_unique<Executor>(
        graph.release(), new BThreadSchedule, new BThreadNotification);
    auto ret = index_.emplace(pb_graph.name(), std::move(executor));
    if (!ret.second) {
      return ECM_ERROR(StatusCode::kAlreadyExists,
                       "graph name {} is already exist", pb_graph.name());
    }
    LOG(INFO) << "load graph " << pb_graph.name() << " " << graph_file_path;
  }

  return OkStatus();
}

} // namespace ecm::taskflow