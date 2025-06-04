#pragma once

#include <deque>

#include "ecm/taskflow/graph.h"
#include "ecm/taskflow/notification.h"
#include "ecm/taskflow/schedule.h"
#include "ecm/taskflow/session.h"

namespace ecm::taskflow {

class Executor {
public:
  Executor(Graph *graph, Schedule *sched, Notification *notification)
      : graph_(graph), sched_(sched), notification_(notification) {}

  Status Execute(Session *session);

private:
  void sched_ready(Session *session, const std::vector<Node *> &ready,
                   std::deque<Node *> *inline_ready);

  void process(Session *session, Node *node);

  void execute_node(Session *session, Node *node);

  void propagate(Session *session, Node *node, std::vector<Node *> *ready);

  std::unique_ptr<Graph> graph_;
  std::unique_ptr<Schedule> sched_;
  std::unique_ptr<Notification> notification_;
};

} // namespace ecm::taskflow