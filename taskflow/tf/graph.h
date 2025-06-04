#pragma once

#include <map>
#include <string>

#include "ecm/core/status.h"
#include "ecm/taskflow/graph.pb.h"
#include "ecm/taskflow/op_kernel.h"
#include "ecm/taskflow/op_registry.h"

namespace ecm::taskflow {

struct Node {
  int id = 0;
  std::string name;
  std::unique_ptr<OpKernel> op;
  NodeProto def;

  std::vector<Node *> input_nodes;
  std::vector<Node *> output_nodes;
};

struct NodeInput {
  NodeInput(const Node *node, int input_idx)
      : node(node), input_idx(input_idx) {}

  const Node *node = nullptr;
  int input_idx = 0;
};

class Graph {
public:
  ~Graph();

  Status Build(const OpRegistry *ops, const GraphProto &graph_proto);

  const std::string &Name() const { return name_; }

  size_t Nodes() const { return nodes_map_.size(); }

  const std::vector<Node *> &GetRoots() const { return roots_; }

  const std::vector<NodeInput> *GetInputNodes(const std::string &input) {
    auto it = inputs_.find(input);
    if (it == inputs_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  const std::set<std::string> &GetOutputs() const { return outputs_; }

  const Node *GetNode(const std::string &name) const {
    auto it = nodes_map_.find(name);
    if (it == nodes_map_.end()) {
      return nullptr;
    }
    return it->second;
  }

  const Node *GetNode(size_t id) const { return nodes_[id]; }

  void DebugPrint() const;

private:
  std::string name_;
  std::map<std::string, Node *> nodes_map_; // owns Node*
  std::vector<Node *> roots_;
  std::vector<Node *> nodes_;                            // indexed by node id
  std::map<std::string, std::vector<NodeInput>> inputs_; // $input:name -> nodes
  std::set<std::string> outputs_;
};

} // namespace ecm::taskflow