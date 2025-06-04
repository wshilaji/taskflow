#include "ecm/taskflow/executor.h"
#include "ecm/metrics/metrics.h"

namespace ecm::taskflow {

TransactionRecorder exec_tr("ecm_tf_exec", {"graph", "node"});
TransactionRecorder fallback_tr("ecm_tf_fallback", {"graph", "node"});

Status Executor::Execute(Session *session) {

  /// check input types
  for (auto &[name, data] : session->data_map_) {
    auto node_inputs = graph_->GetInputNodes(name);
    ECM_RETURN_CHECK(node_inputs != nullptr);
    for (auto &node_input : *node_inputs) {
      auto &input = node_input.node->op->Inputs()[node_input.input_idx];
      if (data->Type() != input.type) {
        return ECM_ERROR(
            StatusCode::kInvalidArgument,
            "input {} type {} != node {} op define type {} in graph {}", name,
            data->Type(), node_input.node->name, input.type, graph_->Name());
      }
    }
  }

  // prepare frame_states
  session->frame_states_.resize(graph_->Nodes());
  for (size_t i = 0; i < graph_->Nodes(); i++) {
    auto &frame = session->frame_states_[i];
    frame.reset(new FrameState);
    frame->node_done.reset(notification_->New());
    auto node = graph_->GetNode(i);
    if (node->op->IsExpensive()) {
      frame->sched_done.reset(notification_->New());
    }
  }
  for (auto root : graph_->GetRoots()) {
    if (root->op->IsExpensive()) {
      continue; // dup reset
    }
    auto &frame = session->frame_states_[root->id];
    frame->sched_done.reset(notification_->New());
  }

  for (auto &output : graph_->GetOutputs()) {
    auto &data = session->data_map_[output];
    (void)data;
  }

  // TODO: remove unused nodes in graph

  auto &ready = graph_->GetRoots();
  sched_ready(session, ready, nullptr);

  for (auto &name : session->outputs_) {
    auto node = graph_->GetNode(name);
    if (node == nullptr) {
      return ECM_ERROR(StatusCode::kNotFound,
                       "can not find node {} in graph {}", name,
                       graph_->Name());
    }
    auto &frame = session->frame_states_[node->id];
    frame->node_done->Wait();
  }

  for (auto &frame : session->frame_states_) {
    frame->node_done->Wait();
    if (frame->sched_done && !frame->is_inline) {
      frame->sched_done->Wait();
    }
  }

  for (auto &frame : session->frame_states_) {
    ECM_RETURN_IF_ERROR(frame->status);
  }

  return Status();
}

void Executor::sched_ready(Session *session, const std::vector<Node *> &ready,
                           std::deque<Node *> *inline_ready) {

  if (ready.empty()) {
    return;
  }

  if (inline_ready == nullptr) {
    // Schedule to run all the ready ops in thread pool.
    for (auto node : ready) {
      sched_->Submit([this, session, node]() { process(session, node); });
    }
    return;
  }

  Node *curr_expensive_node = nullptr;
  for (auto node : ready) {
    if (!node->op->IsExpensive()) {
      // Inline this inexpensive node.
      inline_ready->push_back(node);
    } else {
      if (curr_expensive_node) {
        // Dispatch to another thread since there is plenty of work to
        // do for this thread.
        sched_->Submit([this, session, curr_expensive_node]() {
          process(session, curr_expensive_node);
        });
      }
      curr_expensive_node = node;
    }
  }
  if (curr_expensive_node) {
    if (inline_ready->empty()) {
      // Tail recursion optimization
      inline_ready->push_back(curr_expensive_node);
    } else {
      // There are inline nodes to run already. We dispatch this expensive
      // node to other thread.
      sched_->Submit([this, session, curr_expensive_node]() {
        process(session, curr_expensive_node);
      });
    }
  }
}

void Executor::process(Session *session, Node *node) {
  std::vector<Node *> ready;
  std::deque<Node *> inline_ready;

  auto curr_expensive_node = node;

  inline_ready.push_back(node);
  while (!inline_ready.empty()) {
    node = inline_ready.front();
    inline_ready.pop_front();
    execute_node(session, node);
    if (node != curr_expensive_node) {
      auto &frame = session->frame_states_[node->id];
      frame->is_inline = true;
    }
    propagate(session, node, &ready);
    sched_ready(session, ready, &inline_ready);

    ready.clear();
  }

  auto &frame = session->frame_states_[curr_expensive_node->id];
  frame->sched_done->Notify();
}

void Executor::execute_node(Session *session, Node *node) {
  auto &frame = session->frame_states_[node->id];
  for (auto input : node->input_nodes) {
    auto &in_frame = session->frame_states_[input->id];
    in_frame->node_done->Wait();
    if (!in_frame->status.ok()) {
      frame->node = node;
      frame->status = in_frame->status;
      frame->node_done->Notify();
      return;
    }
  }

  OpKernelContext ctx;
  ctx.node_def = &node->def;
  ctx.session = session;

  frame->node = node;

  frame->status = exec_tr.Capture(
      session->Id(), {graph_->Name(), node->name},
      [node, &ctx]() -> Status { return node->op->Compute(&ctx); });
  if (node->op->EnableFallback() && !frame->status.ok()) {
    (void)fallback_tr.Capture(session->Id(), {graph_->Name(), node->name},
                              [&]() {
                                frame->status = node->op->Fallback(&ctx);
                                return OkStatus();
                              });
  }
  frame->node_done->Notify();
}

void Executor::propagate(Session *session, Node *node,
                         std::vector<Node *> *ready) {
  for (auto output : node->output_nodes) {
    auto &out_frame = session->frame_states_[output->id];
    if (out_frame->IsReady()) {
      continue; // was scheduled
    }
    bool output_is_ready = true;
    for (auto in_node : output->input_nodes) {
      auto &in_frame = session->frame_states_[in_node->id];
      if (!in_frame->node_done->HasBeenNotified()) {
        output_is_ready = false;
      }
    }
    if (output_is_ready && out_frame->SetReady()) {
      // in this schdule
      ready->push_back(output);
    }
  }
}

} // namespace ecm::taskflow