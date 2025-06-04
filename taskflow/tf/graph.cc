#include "ecm/taskflow/graph.h"
#include "absl/strings/str_split.h"

namespace ecm::taskflow {

Graph::~Graph() {
  for (auto &entry : nodes_map_) {
    delete entry.second;
  }
}

Status Graph::Build(const OpRegistry *ops, const GraphProto &graph_proto) {
  name_ = graph_proto.name();

  int id = 0;
  std::map<std::string, Input> output_map;

  for (auto &node_proto : graph_proto.node()) {
    auto node = new Node;
    node->id = id++;
    node->name = node_proto.name();
    auto ret = nodes_map_.emplace(node_proto.name(), node);
    if (!ret.second) {
      delete node;
      return ECM_ERROR(StatusCode::kAlreadyExists,
                       "node {} is already exist in graph {}",
                       node_proto.name(), name_);
    }
    nodes_.push_back(node);

    OpKernelConstruction ctx;
    ctx.node_def = &node_proto;
    node->op.reset(ops->Create(node_proto.op(), &ctx));
    if (node->op == nullptr) {
      return ECM_ERROR(StatusCode::kNotFound,
                       "can not create op {} in graph {}", node_proto.op(),
                       name_);
    }
    node->def = node_proto;
    if (!ctx.status.ok()) {
      return ECM_ERROR(StatusCode::kInvalidArgument,
                       "init op {} in graph {} failed: {}", node_proto.op(),
                       name_, ctx.status.ToString());
    }

    // build outputs
    for (auto &output : node->op->Outputs()) {
      auto output_key = format("{}:{}", node_proto.name(), output.name);
      auto ret = output_map.emplace(output_key, output);
      if (!ret.second) {
        return ECM_ERROR(StatusCode::kAlreadyExists,
                         "output {} is already exist in graph {}", output_key,
                         name_);
      }
      outputs_.insert(output_key);
    }

    // check input
    if ((size_t)node->def.input_size() != node->op->Inputs().size()) {
      return ECM_ERROR(
          StatusCode::kFailedPrecondition,
          "node {} input size {} != op {} input size {} in graph {}",
          node->name, node->def.input_size(), node->def.op(),
          node->op->Inputs().size(), name_);
    }
  }

  for (auto &node_proto : graph_proto.node()) {
    auto node = nodes_map_[node_proto.name()];

    std::set<Node *> in_nodes;
    for (int i = 0; i < node_proto.input_size(); i++) {
      auto &in_name = node_proto.input(i);
      std::vector<std::string> name_parts = absl::StrSplit(in_name, ':');
      if (name_parts.size() != 2) {
        return ECM_ERROR(
            StatusCode::kFailedPrecondition,
            "input {} is not in <node_name>:<output_name> format in graph {}",
            in_name, name_);
      }
      if (name_parts[0] == "$input") {
        auto &nodes = inputs_[in_name];
        nodes.emplace_back(node, i);
        continue;
      }

      // input in graph define
      auto input_it = output_map.find(in_name);
      if (input_it == output_map.end()) {
        return ECM_ERROR(StatusCode::kNotFound,
                         "input {} is not found for node {} in graph {}",
                         in_name, node_proto.name(), name_);
      }

      // input in op define
      auto &input = node->op->Inputs()[i];
      if (input.type != input_it->second.type) {
        return ECM_ERROR(StatusCode::kFailedPrecondition,
                         "{}th input type {} for node {} != output type {} for "
                         "node {} in graph {}",
                         i, input.type, node->name, input_it->second.type,
                         name_parts[0], name_);
      }

      auto in_node_it = nodes_map_.find(name_parts[0]);
      if (in_node_it == nodes_map_.end()) {
        return ECM_ERROR(StatusCode::kNotFound,
                         "can not find input node {} in graph {}",
                         name_parts[0], name_);
      }
      auto in_node = in_node_it->second;

      auto ret = in_nodes.insert(in_node);
      if (ret.second) {
        node->input_nodes.push_back(in_node);
        in_node->output_nodes.push_back(node);
      }
    }
  }

  for (auto &entry : nodes_map_) {
    auto node = entry.second;
    if (node->input_nodes.empty()) {
      roots_.push_back(node);
    }
  }

  // TODO: test graph cycle

  return Status();
}

void Graph::DebugPrint() const {
  std::cout << ">>> graph debug" << std::endl;
  for (auto &entry : nodes_map_) {
    auto node = entry.second;
    std::cout << node->name << " ->";
    for (auto out : node->output_nodes) {
      std::cout << " " << out->name;
    }
    std::cout << std::endl;
  }
  std::cout << ">>> graph debug" << std::endl;
}

} // namespace ecm::taskflow
