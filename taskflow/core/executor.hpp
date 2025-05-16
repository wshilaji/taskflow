#pragma once

#include "observer.hpp"
#include "taskflow.hpp"
#include "async_task.hpp"
#include "freelist.hpp"


namespace tf {

class Executor {

  friend class FlowBuilder;
  friend class Subflow;
  friend class Runtime;
  friend class Algorithm;

  public:

  /**
  @brief constructs the executor with @c N worker threads

  @param N number of workers (default std::thread::hardware_concurrency)
  @param wix interface class instance to configure workers' behaviors

  The constructor spawns @c N worker threads to run tasks in a
  work-stealing loop. The number of workers must be greater than zero
  or an exception will be thrown.
  By default, the number of worker threads is equal to the maximum
  hardware concurrency returned by std::thread::hardware_concurrency.

  Users can alter the worker behavior, such as changing thread affinity,
  via deriving an instance from tf::WorkerInterface.
  */
  explicit Executor(
    size_t N = std::thread::hardware_concurrency(),
    std::shared_ptr<WorkerInterface> wix = nullptr
  );

  ~Executor();

  tf::Future<void> run(Taskflow& taskflow);

  tf::Future<void> run(Taskflow&& taskflow);

  template<typename C>
  tf::Future<void> run(Taskflow& taskflow, C&& callable);

  template<typename C>
  tf::Future<void> run(Taskflow&& taskflow, C&& callable);

  tf::Future<void> run_n(Taskflow& taskflow, size_t N);

  tf::Future<void> run_n(Taskflow&& taskflow, size_t N);

  template<typename C>
  tf::Future<void> run_n(Taskflow& taskflow, size_t N, C&& callable);

  template<typename C>
  tf::Future<void> run_n(Taskflow&& taskflow, size_t N, C&& callable);

  template<typename P>
  tf::Future<void> run_until(Taskflow& taskflow, P&& pred);

  template<typename P>
  tf::Future<void> run_until(Taskflow&& taskflow, P&& pred);

  template<typename P, typename C>
  tf::Future<void> run_until(Taskflow& taskflow, P&& pred, C&& callable);

  template<typename P, typename C>
  tf::Future<void> run_until(Taskflow&& taskflow, P&& pred, C&& callable);

  template <typename T>
  void corun(T& target);

  template <typename P>
  void corun_until(P&& predicate);

  void wait_for_all();

  size_t num_workers() const noexcept;
  
  /**
  @brief queries the number of queues used in the work-stealing loop
  */
  size_t num_queues() const noexcept;
  size_t num_topologies() const;
  size_t num_taskflows() const;
  int this_worker_id() const;
 
  // --------------------------------------------------------------------------
  // Observer methods
  // --------------------------------------------------------------------------

  template <typename Observer, typename... ArgsT>
  std::shared_ptr<Observer> make_observer(ArgsT&&... args);

  /**
  @brief removes an observer from the executor

  This member function is not thread-safe.
  */
  template <typename Observer>
  void remove_observer(std::shared_ptr<Observer> observer);

  /**
  @brief queries the number of observers
  */
  size_t num_observers() const noexcept;

  // --------------------------------------------------------------------------
  // Async Task Methods
  // --------------------------------------------------------------------------
  
  template <typename P, typename F>
  auto async(P&& params, F&& func);

  template <typename F>
  auto async(F&& func);

  template <typename P, typename F>
  void silent_async(P&& params, F&& func);
  
  template <typename F>
  void silent_async(F&& func);

  template <typename F, typename... Tasks,
    std::enable_if_t<all_same_v<AsyncTask, std::decay_t<Tasks>...>, void>* = nullptr
  >
  tf::AsyncTask silent_dependent_async(F&& func, Tasks&&... tasks);
  
  template <typename P, typename F, typename... Tasks,
    std::enable_if_t<is_task_params_v<P> && all_same_v<AsyncTask, std::decay_t<Tasks>...>, void>* = nullptr
  >
  tf::AsyncTask silent_dependent_async(P&& params, F&& func, Tasks&&... tasks);
  
  template <typename F, typename I, 
    std::enable_if_t<!std::is_same_v<std::decay_t<I>, AsyncTask>, void>* = nullptr
  >
  tf::AsyncTask silent_dependent_async(F&& func, I first, I last);
  
  template <typename P, typename F, typename I, 
    std::enable_if_t<is_task_params_v<P> && !std::is_same_v<std::decay_t<I>, AsyncTask>, void>* = nullptr
  >
  tf::AsyncTask silent_dependent_async(P&& params, F&& func, I first, I last);
  
  // --------------------------------------------------------------------------
  // Dependent Async Methods
  // --------------------------------------------------------------------------
  
  template <typename F, typename... Tasks,
    std::enable_if_t<all_same_v<AsyncTask, std::decay_t<Tasks>...>, void>* = nullptr
  >
  auto dependent_async(F&& func, Tasks&&... tasks);
  
  template <typename P, typename F, typename... Tasks,
    std::enable_if_t<is_task_params_v<P> && all_same_v<AsyncTask, std::decay_t<Tasks>...>, void>* = nullptr
  >
  auto dependent_async(P&& params, F&& func, Tasks&&... tasks);
  
  template <typename F, typename I,
    std::enable_if_t<!std::is_same_v<std::decay_t<I>, AsyncTask>, void>* = nullptr
  >
  auto dependent_async(F&& func, I first, I last);
  
  template <typename P, typename F, typename I,
    std::enable_if_t<is_task_params_v<P> && !std::is_same_v<std::decay_t<I>, AsyncTask>, void>* = nullptr
  >
  auto dependent_async(P&& params, F&& func, I first, I last);

  private:
    
  std::mutex _taskflows_mutex;
  
  std::vector<Worker> _workers;
  DefaultNotifier _notifier;

#if __cplusplus >= TF_CPP20
  //std::latch _latch;
  std::atomic<size_t> _num_topologies {0};
#else
  //Latch _latch;
  std::condition_variable _topology_cv;
  std::mutex _topology_mutex;
  size_t _num_topologies {0};
#endif
  
  std::list<Taskflow> _taskflows;

  Freelist<Node*> _buffers; //只有这个是tsq无界
                            //worker里面的wsq是有界的

  std::shared_ptr<WorkerInterface> _worker_interface;
  std::unordered_set<std::shared_ptr<ObserverInterface>> _observers;

  void _observer_prologue(Worker&, Node*);
  void _observer_epilogue(Worker&, Node*);
  void _spawn(size_t);
  void _exploit_task(Worker&, Node*&);
  bool _explore_task(Worker&, Node*&);
  void _schedule(Worker&, Node*);
  void _schedule(Node*);
  void _set_up_topology(Worker*, Topology*);
  void _tear_down_topology(Worker&, Topology*); // 拆除拓扑
  void _tear_down_async(Worker&, Node*, Node*&); // tear down 拆除
  void _tear_down_dependent_async(Worker&, Node*, Node*&);
  void _tear_down_invoke(Worker&, Node*, Node*&);
  void _increment_topology();
  void _decrement_topology();
  void _invoke(Worker&, Node*);
  void _invoke_static_task(Worker&, Node*);
  void _invoke_condition_task(Worker&, Node*, SmallVector<int>&);
  void _invoke_multi_condition_task(Worker&, Node*, SmallVector<int>&);
  void _process_async_dependent(Node*, tf::AsyncTask&, size_t&);
  void _process_exception(Worker&, Node*);
  void _schedule_async_task(Node*);
  void _update_cache(Worker&, Node*&, Node*);

  bool _wait_for_task(Worker&, Node*&);
  bool _invoke_subflow_task(Worker&, Node*);
  bool _invoke_module_task(Worker&, Node*);
  bool _invoke_module_task_impl(Worker&, Node*, Graph&);
  bool _invoke_async_task(Worker&, Node*);
  bool _invoke_dependent_async_task(Worker&, Node*);
  bool _invoke_runtime_task(Worker&, Node*);
  bool _invoke_runtime_task_impl(Worker&, Node*, std::function<void(Runtime&)>&);
  bool _invoke_runtime_task_impl(Worker&, Node*, std::function<void(Runtime&, bool)>&);

  template <typename I>
  I _set_up_graph(I, I, Topology*, Node*);
  
  template <typename P>
  void _corun_until(Worker&, P&&);
  
  template <typename I>
  void _corun_graph(Worker&, Node*, I, I);

  template <typename I>
  void _schedule(Worker&, I, I);

  template <typename I>
  void _schedule(I, I);

  template <typename I>
  void _schedule_graph_with_parent(Worker&, I, I, Node*);

  template <typename P, typename F>
  auto _async(P&&, F&&, Topology*, Node*);

  template <typename P, typename F>
  void _silent_async(P&&, F&&, Topology*, Node*);

};

#ifndef DOXYGEN_GENERATING_OUTPUT

// Constructor
inline Executor::Executor(size_t N, std::shared_ptr<WorkerInterface> wix) :
  _workers         (N),
  _notifier        (N),
  //_latch           (N+1),
  _buffers        (N),
  _worker_interface(std::move(wix)) {

  if(N == 0) {
    TF_THROW("executor must define at least one worker");
  }

  _spawn(N);

  // initialize the default observer if requested
  if(has_env(TF_ENABLE_PROFILER)) {
    TFProfManager::get()._manage(make_observer<TFProfObserver>());
  }
}

// Destructor
inline Executor::~Executor() {

  // wait for all topologies to complete
  wait_for_all();

  // shut down the scheduler
  for(size_t i=0; i<_workers.size(); ++i) {
  #if __cplusplus >= TF_CPP20
    _workers[i]._done.test_and_set(std::memory_order_relaxed);
  #else
    _workers[i]._done.store(true, std::memory_order_relaxed);
  #endif
  }

  _notifier.notify_all(); //避免有线程死等

  for(auto& w : _workers) {
    w._thread.join();
  }
}

// Function: num_workers
inline size_t Executor::num_workers() const noexcept {
  return _workers.size();
}

// Function: num_queues
inline size_t Executor::num_queues() const noexcept {
  return _workers.size() + _buffers.size();
}

// Function: num_topologies
inline size_t Executor::num_topologies() const {
#if __cplusplus >= TF_CPP20
  return _num_topologies.load(std::memory_order_relaxed);
#else
  return _num_topologies;
#endif
}

// Function: num_taskflows
inline size_t Executor::num_taskflows() const {
  return _taskflows.size();
}

// Function: this_worker_id
inline int Executor::this_worker_id() const {
    // dysNote 注意在主进程里面 tf.Executor executor; 里面这个this_worker 也是有的， 这个是thread_local 但是没有初始化是nullptr
  auto w = pt::this_worker;
  return (w && w->_executor == this) ? static_cast<int>(w->_id) : -1;
  // 如果是在其他线程pt::this_worker = &w; _worker[id]._executor == this; 然后如果_worker[id]._executor->this_worker_id()就是线程id
}

// Procedure: _spawn
inline void Executor::_spawn(size_t N) {

  // Note: We cannot declare latch as a local variable here because the main thread
  // may exit earlier than other threads, causing the latch to be destroyed
  // while other threads are still accessing it, leading to undefined behavior.
  for(size_t id=0; id<N; ++id) {

    _workers[id]._id = id;
    _workers[id]._vtm = id;
    _workers[id]._executor = this;
    _workers[id]._waiter = &_notifier._waiters[id];
    _workers[id]._thread = std::thread([&, &w=_workers[id]] () {

      pt::this_worker = &w;

      // synchronize with the main thread to ensure all worker data has been set
      //_latch.arrive_and_wait(); 
      
      // initialize the random engine and seed for work-stealing loop
      w._rdgen.seed(static_cast<std::default_random_engine::result_type>(
        std::hash<std::thread::id>()(std::this_thread::get_id()))
      );
      //w._udist = std::uniform_int_distribution<size_t>(0, _workers.size() - 1);
      // 如果distribution(0, 10)是生成整数0,1,2,..10一直到10都会生成  会包括右边界10,  但是这里是-2不应该是-1吗
      w._udist = std::uniform_int_distribution<size_t>(0, _workers.size() + _buffers.size() - 2);

      // before entering the work-stealing loop, call the scheduler prologue
      if(_worker_interface) {
        _worker_interface->scheduler_prologue(w);
      }

      Node* t = nullptr;
      std::exception_ptr ptr = nullptr;

      // must use 1 as condition instead of !done because
      // the previous worker may stop while the following workers
      // are still preparing for entering the scheduling loop
      try {

        // worker loop
        while(1) {

          // drain out the local queue 清空本地队列
          _exploit_task(w, t); //第一轮里面t是null所以走了wait_for_task  后面当worker当wsq队列有node先while执行自己队列不窃取

          // steal and wait for tasks
          if(_wait_for_task(w, t) == false) {
            break;
          }
        }
      } 
      catch(...) {
        ptr = std::current_exception();
      }
      
      // call the user-specified epilogue function
      if(_worker_interface) {
        _worker_interface->scheduler_epilogue(w, ptr);
      }

    });
  } 

  //_latch.arrive_and_wait();
}

// Function: _corun_until
template <typename P>
void Executor::_corun_until(Worker& w, P&& stop_predicate) {
  
  const size_t MAX_STEALS = ((num_queues() + 1) << 1);
  
  exploit:

  while(!stop_predicate()) {

    if(auto t = w._wsq.pop(); t) {
      _invoke(w, t);
    }
    else {
      size_t num_steals = 0;

      explore:

      //t = (w._id == w._vtm) ? _wsq.steal() : _workers[w._vtm]._wsq.steal();
      t = (w._vtm < _workers.size()) ? _workers[w._vtm]._wsq.steal() : 
                                       _buffers.steal(w._vtm - _workers.size());

      if(t) {
        _invoke(w, t);
        goto exploit;
      }
      else if(!stop_predicate()) {
        if(++num_steals > MAX_STEALS) {
          std::this_thread::yield();
        }
        w._vtm = w._rdvtm();
        goto explore;
      }
      else {
        break;
      }
    }
  }
}

// Function: _explore_task
inline bool Executor::_explore_task(Worker& w, Node*& t) {

  //assert(!t);
  
  const size_t MAX_STEALS = ((num_queues() + 1) << 1);

  size_t num_steals = 0;
  size_t num_empty_steals = 0;

  // Make the worker steal immediately from the assigned victim.
  while(true) {
    // If the worker's victim thread (w._vtm) is within the worker pool, steal from the worker's queue.
    // Otherwise, steal from the buffer, adjusting the victim index based on the worker pool size.
    //
    // //t = (w._id == w._vtm) ? _freelist.steal(w._id) : _workers[w._vtm]._wsq.steal(); // 老版本2
    // 第一轮的时候 vtm = id 从0->N 所以第一轮都没任务的时候只会超下一个queue找。 worker[vtm].wsq ||  buffer[vtm].wsq
    t = (w._vtm < _workers.size())
      ? _workers[w._vtm]._wsq.steal_with_hint(num_empty_steals)
      : _buffers.steal_with_hint(w._vtm - _workers.size(), num_empty_steals);

    if(t) { //  dysNote 只有找到task才停止 第一轮从别的worker都会找不到。然后朝下执行 重算vtm 估计一直到FreeList buffer里面找到task 然后所有线程worker里面的vtm都打散
      break;
    }

    // Increment the steal count, and if it exceeds MAX_STEALS, yield the thread.
    // If the number of *consecutive* empty steals reaches MAX_STEALS, exit the loop.
    if (++num_steals > MAX_STEALS) {
      std::this_thread::yield();
      if(num_empty_steals == MAX_STEALS) {
        break;
      }
    }

  #if __cplusplus >= TF_CPP20
    if(w._done.test(std::memory_order_relaxed)) {
  #else
    if(w._done.load(std::memory_order_relaxed)) {
  #endif
      return false; // 仅仅在.done为true的时候返回false
    } 
    
    // Randomely generate a next victim.
    w._vtm = w._rdvtm(); //负载均衡用的？ dysNote 注意这里解释了为什么 
        // 是 -2 ，这里w._udist = std::uniform_int_distribution<size_t>(0, _workers.size() + _buffers.size() - 2);
        // 假设work_size + que.size 是10 ， 这个时候随机桶范围是<0,..7,8>左右边界都能取到 ， 会觉得这么不是少了个下标9吗
        // 是因为_rdvtm()函数会 + 1 所以当时揉到8时候。会+1到9 。之所以这样是rdvtm为了避开揉到自己本身id
  } 

  return true;
}

// Procedure: _exploit_task
inline void Executor::_exploit_task(Worker& w, Node*& t) {
  while(t) {
    _invoke(w, t);
    t = w._wsq.pop();
  }
}

// Function: _wait_for_task
inline bool Executor::_wait_for_task(Worker& w, Node*& t) {

  explore_task:

  if(_explore_task(w, t) == false) {
    return false;
  }
  
  // Go exploit the task if we successfully steal one.
  if(t) {
    return true;
  }

  // Entering the 2PC guard as all queues should be empty after many stealing attempts.
  _notifier.prepare_wait(w._waiter);
  
  // Condition #1: buffers should be empty
  for(size_t vtm=0; vtm<_buffers.size(); ++vtm) {
    if(!_buffers._buckets[vtm].queue.empty()) {
      w._vtm = vtm + _workers.size();
      _notifier.cancel_wait(w._waiter);
      goto explore_task;
    }
  }
  
  // Condition #2: worker queues should be empty
  // Note: We need to use index-based looping to avoid data race with _spawan
  // which initializes other worker data structure at the same time
  for(size_t vtm=0; vtm<w._id; ++vtm) {
    if(!_workers[vtm]._wsq.empty()) {
      w._vtm = vtm;
      _notifier.cancel_wait(w._waiter);
      goto explore_task;
    }
  }
  
  // due to the property of the work-stealing queue, we don't need to check
  // the queue of this worker
  for(size_t vtm=w._id+1; vtm<_workers.size(); vtm++) {
    if(!_workers[vtm]._wsq.empty()) {
      w._vtm = vtm;
      _notifier.cancel_wait(w._waiter);
      goto explore_task;
    }
  }
  
  // Condition #3: worker should be alive
#if __cplusplus >= TF_CPP20
  if(w._done.test(std::memory_order_relaxed)) {
#else
  if(w._done.load(std::memory_order_relaxed)) {
#endif
    _notifier.cancel_wait(w._waiter);
    return false;
  }
  
  // Now I really need to relinquish my self to others.
  _notifier.commit_wait(w._waiter); //等着。 等wait唤醒
  goto explore_task;
}

// Function: make_observer
template<typename Observer, typename... ArgsT>
std::shared_ptr<Observer> Executor::make_observer(ArgsT&&... args) {

  static_assert(
    std::is_base_of_v<ObserverInterface, Observer>,
    "Observer must be derived from ObserverInterface"
  );

  // use a local variable to mimic the constructor
  auto ptr = std::make_shared<Observer>(std::forward<ArgsT>(args)...);

  ptr->set_up(_workers.size());

  _observers.emplace(std::static_pointer_cast<ObserverInterface>(ptr));

  return ptr;
}

// Procedure: remove_observer
template <typename Observer>
void Executor::remove_observer(std::shared_ptr<Observer> ptr) {

  static_assert(
    std::is_base_of_v<ObserverInterface, Observer>,
    "Observer must be derived from ObserverInterface"
  );

  _observers.erase(std::static_pointer_cast<ObserverInterface>(ptr));
}

// Function: num_observers
inline size_t Executor::num_observers() const noexcept {
  return _observers.size();
}

// Procedure: _schedule
inline void Executor::_schedule(Worker& worker, Node* node) {
  
  // caller is a worker to this pool - starting at v3.5 we do not use
  // any complicated notification mechanism as the experimental result
  // has shown no significant advantage.
  if(worker._executor == this) {
    worker._wsq.push(node, [&](){ _buffers.push(node); });
    _notifier.notify_one();
    return;
  }
  
  // go through the centralized queue
  _buffers.push(node);
  _notifier.notify_one();
}

// Procedure: _schedule
inline void Executor::_schedule(Node* node) {
  _buffers.push(node);
  _notifier.notify_one();
}

// Procedure: _schedule
template <typename I>
void Executor::_schedule(Worker& worker, I first, I last) {

  size_t num_nodes = last - first;
  
  if(num_nodes == 0) {
    return;
  }
  
  // NOTE: We cannot use first/last in the for-loop (e.g., for(; first != last; ++first)).
  // This is because when a node v is inserted into the queue, v can run and finish 
  // immediately. If v is the last node in the graph, it will tear down the parent task vector
  // which cause the last ++first to fail. This problem is specific to MSVC which has a stricter
  // iterator implementation in std::vector than GCC/Clang.
  if(worker._executor == this) {
    for(size_t i=0; i<num_nodes; i++) {
      auto node = detail::get_node_ptr(first[i]);
      worker._wsq.push(node, [&](){ _buffers.push(node); }); // wsq满了。 onfull塞到buffers里面 freelist是无界的
      _notifier.notify_one();
    }
    return;
  }
  
  for(size_t i=0; i<num_nodes; i++) {
    _buffers.push(detail::get_node_ptr(first[i]));
  }
  _notifier.notify_n(num_nodes);
}

// Procedure: _schedule
template <typename I>
inline void Executor::_schedule(I first, I last) {
  
  size_t num_nodes = last - first;

  if(num_nodes == 0) {
    return;
  }

  // NOTE: We cannot use first/last in the for-loop (e.g., for(; first != last; ++first)).
  // This is because when a node v is inserted into the queue, v can run and finish 
  // immediately. If v is the last node in the graph, it will tear down the parent task vector
  // which cause the last ++first to fail. This problem is specific to MSVC which has a stricter
  // iterator implementation in std::vector than GCC/Clang.
  for(size_t i=0; i<num_nodes; i++) {
    _buffers.push(detail::get_node_ptr(first[i]));
  }
  _notifier.notify_n(num_nodes);
}
  
template <typename I>
void Executor::_schedule_graph_with_parent(Worker& worker, I beg, I end, Node* parent) {
  auto send = _set_up_graph(beg, end, parent->_topology, parent);
  parent->_join_counter.fetch_add(send - beg, std::memory_order_relaxed);
  _schedule(worker, beg, send);
}

TF_FORCE_INLINE void Executor::_update_cache(Worker& worker, Node*& cache, Node* node) {
  if(cache) {
    _schedule(worker, cache);
  }
  cache = node;
}
  
// Procedure: _invoke
inline void Executor::_invoke(Worker& worker, Node* node) {

  #define TF_INVOKE_CONTINUATION()  \
  if (cache) {                      \
    node = cache;                   \
    goto begin_invoke;              \
  }

  begin_invoke:

  Node* cache {nullptr};
  
  // if this is the second invoke due to preemption, directly jump to invoke task
  if(node->_nstate & NSTATE::PREEMPTED) { // 如果是抢占
    goto invoke_task;
  }

  // if the work has been cancelled, there is no need to continue
  if(node->_is_cancelled()) {
    _tear_down_invoke(worker, node, cache); // tear down拆除
    TF_INVOKE_CONTINUATION();
    return;
  }

  // if acquiring semaphore(s) exists, acquire them first
  if(node->_semaphores && !node->_semaphores->to_acquire.empty()) {
    SmallVector<Node*> waiters;
    if(!node->_acquire_all(waiters)) {
      _schedule(worker, waiters.begin(), waiters.end());
      return;
    }
  }
  
  invoke_task:
  
  SmallVector<int> conds;

  // switch is faster than nested if-else due to jump table
  switch(node->_handle.index()) {
    // static task
    case Node::STATIC:{
      _invoke_static_task(worker, node);
    }
    break;
    
    // runtime task
    case Node::RUNTIME:{
      if(_invoke_runtime_task(worker, node)) {
        return;
      }
    }
    break;

    // subflow task
    case Node::SUBFLOW: {
      if(_invoke_subflow_task(worker, node)) {
        return;
      }
    }
    break;

    // condition task
    case Node::CONDITION: {
      _invoke_condition_task(worker, node, conds);
    }
    break;

    // multi-condition task
    case Node::MULTI_CONDITION: {
      _invoke_multi_condition_task(worker, node, conds);
    }
    break;

    // module task
    case Node::MODULE: {
      if(_invoke_module_task(worker, node)) {
        return;
      }
    }
    break;

    // async task
    case Node::ASYNC: {
      if(_invoke_async_task(worker, node)) {
        return;
      }
      _tear_down_async(worker, node, cache);
      TF_INVOKE_CONTINUATION();
      return;
    }
    break;

    // dependent async task
    case Node::DEPENDENT_ASYNC: {
      if(_invoke_dependent_async_task(worker, node)) {
        return;
      }
      _tear_down_dependent_async(worker, node, cache);
      TF_INVOKE_CONTINUATION();
      return;
    }
    break;

    // monostate (placeholder)
    default:
    break;
  }

  // if releasing semaphores exist, release them
  if(node->_semaphores && !node->_semaphores->to_release.empty()) {
    SmallVector<Node*> waiters;
    node->_release_all(waiters);
    _schedule(worker, waiters.begin(), waiters.end());
  }

  // Reset the join counter with strong dependencies to support cycles.
  // + We must do this before scheduling the successors to avoid race
  //   condition on _predecessors.
  // + We must use fetch_add instead of direct assigning
  //   because the user-space call on "invoke" may explicitly schedule 
  //   this task again (e.g., pipeline) which can access the join_counter.
  node->_join_counter.fetch_add(
    node->num_predecessors() - (node->_nstate & ~NSTATE::MASK), std::memory_order_relaxed
  );

  // acquire the parent flow counter
  auto& join_counter = (node->_parent) ? node->_parent->_join_counter :
                       node->_topology->_join_counter;

  //执行完工作函数后，如果是静态节点，将其后继节点的强依赖数减一，如果减到了0， 
  //则将该节点加入就绪队列，并将拓扑的join_counter加一；如果该节点是条件节点，直接将其后继节点加入就绪队列，并将拓扑的join_counter加一。
  //最后，在_tear_down_invoke函数中将topology->_join_counter减一(因为本次Executor::_invoke消耗了一个就绪节点)，如果减到0，则发出整个taskflow已经执行完毕的通知。
  // Invoke the task based on the corresponding type
  switch(node->_handle.index()) {

    // condition and multi-condition tasks
    // A ----> B
    //   |---> C
    //   |---> D // return {0, 1, 2} //注意返回的是结点的标号
    //
    //   a -0-> d
    //     -1-> e 
    //  b -> e // b , c 强依赖e 只有b.c都执行完。才能执行e。  弱依赖a若条件1  执行e  也就是说一个节点在一次拓扑执行过程中是可以被重复多次触发的。
    //  c->e 
    case Node::CONDITION:
    case Node::MULTI_CONDITION: {
      for(auto cond : conds) {
        if(cond >= 0 && static_cast<size_t>(cond) < node->_num_successors) {
          auto s = node->_edges[cond]; 
          // zeroing the join counter for invariant
          s->_join_counter.store(0, std::memory_order_relaxed);
          join_counter.fetch_add(1, std::memory_order_relaxed);
          _update_cache(worker, cache, s); // 老版本_schedule(worker, s);
        }
      }
    }
    break;

    // non-condition task
    default: {
      for(size_t i=0; i<node->_num_successors; ++i) {
        //if(auto s = node->_successors[i]; --(s->_join_counter) == 0) {
        if(auto s = node->_edges[i]; s->_join_counter.fetch_sub(1, std::memory_order_acq_rel) == 1) { // ==1 是fetch_sub成功返回的老val所以这里是==0
          join_counter.fetch_add(1, std::memory_order_relaxed);
          _update_cache(worker, cache, s); // 老版本_schedule(worker, s);
        }
      }
    }
    break;
  }
  
  // clean up the node after execution
  _tear_down_invoke(worker, node, cache);
  TF_INVOKE_CONTINUATION();
}

// Procedure: _tear_down_invoke
inline void Executor::_tear_down_invoke(Worker& worker, Node* node, Node*& cache) {
  
  // we must check parent first before subtracting the join counter,
  // or it can introduce data race
  if(auto parent = node->_parent; parent == nullptr) { // 注意这里是==  如果这里是parent不存在
    if(node->_topology->_join_counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      _tear_down_topology(worker, node->_topology);
    }
  }
  else {  
    // needs to fetch every data before join counter becomes zero at which
    // the node may be deleted
    auto state = parent->_nstate;
    if(parent->_join_counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if(state & NSTATE::PREEMPTED) {
        _update_cache(worker, cache, parent);
      }
    }
  }
}

// Procedure: _observer_prologue
inline void Executor::_observer_prologue(Worker& worker, Node* node) {
  for(auto& observer : _observers) {
    observer->on_entry(WorkerView(worker), TaskView(*node));
  }
}

// Procedure: _observer_epilogue
inline void Executor::_observer_epilogue(Worker& worker, Node* node) {
  for(auto& observer : _observers) {
    observer->on_exit(WorkerView(worker), TaskView(*node));
  }
}

// Procedure: _process_exception
inline void Executor::_process_exception(Worker&, Node* node) {

  constexpr static auto flag = ESTATE::EXCEPTION | ESTATE::CANCELLED;

  // find the anchor and mark the entire path with exception so recursive
  // or nested tasks can be cancelled properly
  // since exception can come from asynchronous task (with runtime), the node
  // itself can be anchored
  auto anchor = node;
  while(anchor && (anchor->_estate.load(std::memory_order_relaxed) & ESTATE::ANCHORED) == 0) {
    anchor->_estate.fetch_or(flag, std::memory_order_relaxed);
    anchor = anchor->_parent;
  }

  // the exception occurs under a blocking call (e.g., corun, join)
  if(anchor) {
    // multiple tasks may throw, and we only take the first thrown exception
    if((anchor->_estate.fetch_or(flag, std::memory_order_relaxed) & ESTATE::EXCEPTION) == 0) {
      anchor->_exception_ptr = std::current_exception();
      return;
    }
  }
  // otherwise, we simply store the exception in the topology and cancel it
  else if(auto tpg = node->_topology; tpg) {
    // multiple tasks may throw, and we only take the first thrown exception
    if((tpg->_estate.fetch_or(flag, std::memory_order_relaxed) & ESTATE::EXCEPTION) == 0) {
      tpg->_exception_ptr = std::current_exception();
      return;
    }
  }
  
  // for now, we simply store the exception in this node; this can happen in an 
  // execution that does not have any external control to capture the exception,
  // such as silent async task
  node->_exception_ptr = std::current_exception();
}

// Procedure: _invoke_static_task
inline void Executor::_invoke_static_task(Worker& worker, Node* node) {
  _observer_prologue(worker, node);
  TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
    std::get_if<Node::Static>(&node->_handle)->work();
  });
  _observer_epilogue(worker, node);
}

// Procedure: _invoke_subflow_task
inline bool Executor::_invoke_subflow_task(Worker& worker, Node* node) {
    
  auto& h = *std::get_if<Node::Subflow>(&node->_handle);
  auto& g = h.subgraph;

  if((node->_nstate & NSTATE::PREEMPTED) == 0) {
    
    // set up the subflow
    Subflow sf(*this, worker, node, g);

    // invoke the subflow callable
    _observer_prologue(worker, node);
    TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
      h.work(sf);
    });
    _observer_epilogue(worker, node);
    
    // spawn the subflow if it is joinable and its graph is non-empty
    // implicit join is faster than Subflow::join as it does not involve corun
    if(sf.joinable() && g.size()) {

      // signal the executor to preempt this node
      node->_nstate |= NSTATE::PREEMPTED;

      // set up and schedule the graph
      _schedule_graph_with_parent(worker, g.begin(), g.end(), node);
      return true;
    }
  }
  else {
    node->_nstate &= ~NSTATE::PREEMPTED;
  }

  // the subflow has finished or joined
  if((node->_nstate & NSTATE::RETAIN_SUBFLOW) == 0) { // 如果subflow没有retain则自动清掉当join后
    g.clear();
  }

  return false;
}

// Procedure: _invoke_condition_task
inline void Executor::_invoke_condition_task(
  Worker& worker, Node* node, SmallVector<int>& conds
) {
  _observer_prologue(worker, node);
  TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
    auto& work = std::get_if<Node::Condition>(&node->_handle)->work;
    conds = { work() };
  });
  _observer_epilogue(worker, node);
}

// Procedure: _invoke_multi_condition_task
inline void Executor::_invoke_multi_condition_task(
  Worker& worker, Node* node, SmallVector<int>& conds
) {
  _observer_prologue(worker, node);
  TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
    conds = std::get_if<Node::MultiCondition>(&node->_handle)->work();
  });
  _observer_epilogue(worker, node);
}

// Procedure: _invoke_module_task
inline bool Executor::_invoke_module_task(Worker& w, Node* node) {
  return _invoke_module_task_impl(w, node, std::get_if<Node::Module>(&node->_handle)->graph);  
}

// Procedure: _invoke_module_task_impl
inline bool Executor::_invoke_module_task_impl(Worker& w, Node* node, Graph& graph) {

  // No need to do anything for empty graph
  if(graph.empty()) {
    return false;
  }

  // first entry - not spawned yet
  if((node->_nstate & NSTATE::PREEMPTED) == 0) {
    // signal the executor to preempt this node
    node->_nstate |= NSTATE::PREEMPTED;
    _schedule_graph_with_parent(w, graph.begin(), graph.end(), node);
    return true;
  }

  // second entry - already spawned
  node->_nstate &= ~NSTATE::PREEMPTED;

  return false;
}


// Procedure: _invoke_async_task
inline bool Executor::_invoke_async_task(Worker& worker, Node* node) {
  auto& work = std::get_if<Node::Async>(&node->_handle)->work;
  switch(work.index()) {
    // void()
    case 0:
      _observer_prologue(worker, node);
      TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
        std::get_if<0>(&work)->operator()();
      });
      _observer_epilogue(worker, node);
    break;
    
    // void(Runtime&)
    case 1:
      if(_invoke_runtime_task_impl(worker, node, *std::get_if<1>(&work))) {
        return true;
      }
    break;
    
    // void(Runtime&, bool)
    case 2:
      if(_invoke_runtime_task_impl(worker, node, *std::get_if<2>(&work))) {
        return true;
      }
    break;
  }

  return false;
}

// Procedure: _invoke_dependent_async_task
inline bool Executor::_invoke_dependent_async_task(Worker& worker, Node* node) {
  auto& work = std::get_if<Node::DependentAsync>(&node->_handle)->work;
  switch(work.index()) {
    // void()
    case 0:
      _observer_prologue(worker, node);
      TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
        std::get_if<0>(&work)->operator()();
      });
      _observer_epilogue(worker, node);
    break;
    
    // void(Runtime&) - silent async
    case 1:
      if(_invoke_runtime_task_impl(worker, node, *std::get_if<1>(&work))) {
        return true;
      }
    break;

    // void(Runtime&, bool) - async
    case 2:
      if(_invoke_runtime_task_impl(worker, node, *std::get_if<2>(&work))) {
        return true;
      }
    break;
  }
  return false;
}

// Function: run
inline tf::Future<void> Executor::run(Taskflow& f) {
  return run_n(f, 1, [](){});
}

// Function: run
inline tf::Future<void> Executor::run(Taskflow&& f) {
  return run_n(std::move(f), 1, [](){});
}

// Function: run
template <typename C>
tf::Future<void> Executor::run(Taskflow& f, C&& c) {
  return run_n(f, 1, std::forward<C>(c));
}

// Function: run
template <typename C>
tf::Future<void> Executor::run(Taskflow&& f, C&& c) {
  return run_n(std::move(f), 1, std::forward<C>(c));
}

// Function: run_n
inline tf::Future<void> Executor::run_n(Taskflow& f, size_t repeat) {
  return run_n(f, repeat, [](){});
}

// Function: run_n
inline tf::Future<void> Executor::run_n(Taskflow&& f, size_t repeat) {
  return run_n(std::move(f), repeat, [](){});
}

// Function: run_n
template <typename C>
tf::Future<void> Executor::run_n(Taskflow& f, size_t repeat, C&& c) {
  return run_until(
    f, [repeat]() mutable { return repeat-- == 0; }, std::forward<C>(c)
  );
}

// Function: run_n
template <typename C>
tf::Future<void> Executor::run_n(Taskflow&& f, size_t repeat, C&& c) {
  return run_until(
    std::move(f), [repeat]() mutable { return repeat-- == 0; }, std::forward<C>(c)
  );
}

// Function: run_until
template<typename P>
tf::Future<void> Executor::run_until(Taskflow& f, P&& pred) {
  return run_until(f, std::forward<P>(pred), [](){});
}

// Function: run_until
template<typename P>
tf::Future<void> Executor::run_until(Taskflow&& f, P&& pred) {
  return run_until(std::move(f), std::forward<P>(pred), [](){});
}

// Function: run_until
template <typename P, typename C>
tf::Future<void> Executor::run_until(Taskflow& f, P&& p, C&& c) { // callable C是执行完之后运行的 pred  条件

  _increment_topology();

  //// Need to check the empty under the lock since subflow task may
  //// define detached blocks that modify the taskflow at the same time
  //bool empty;
  //{
  //  std::lock_guard<std::mutex> lock(f._mutex);
  //  empty = f.empty();
  //}

  // No need to create a real topology but returns an dummy future
  if(f.empty() || p()) {
    c();
    std::promise<void> promise;
    promise.set_value();
    _decrement_topology();
    return tf::Future<void>(promise.get_future());
  }

  // 创建 Topology 这个Topology 初始化时候还塞了taskflow真的是各种依赖
  auto t = std::make_shared<Topology>(f, std::forward<P>(p), std::forward<C>(c));

  // need to create future before the topology got torn down quickly
  tf::Future<void> future(t->_promise.get_future(), t);

  // modifying topology needs to be protected under the lock
  {
    std::lock_guard<std::mutex> lock(f._mutex);
    f._topologies.push(t);
    if(f._topologies.size() == 1) {
      _set_up_topology(pt::this_worker, t.get());
    }
  }

  return future;
}

// Function: run_until
template <typename P, typename C>
tf::Future<void> Executor::run_until(Taskflow&& f, P&& pred, C&& c) {

  std::list<Taskflow>::iterator itr;

  {
    std::scoped_lock<std::mutex> lock(_taskflows_mutex);
    itr = _taskflows.emplace(_taskflows.end(), std::move(f));
    itr->_satellite = itr;
  }

  return run_until(*itr, std::forward<P>(pred), std::forward<C>(c));
}

// Function: corun
template <typename T>
void Executor::corun(T& target) {

  static_assert(has_graph_v<T>, "target must define a member function 'Graph& graph()'");
  
  if(pt::this_worker == nullptr || pt::this_worker->_executor != this) {
    TF_THROW("corun must be called by a worker of the executor");
  }

  Node anchor;
  _corun_graph(*pt::this_worker, &anchor, target.graph().begin(), target.graph().end());
}

// Function: corun_until
template <typename P>
void Executor::corun_until(P&& predicate) {
  
  if(pt::this_worker == nullptr || pt::this_worker->_executor != this) {
    TF_THROW("corun_until must be called by a worker of the executor");
  }

  _corun_until(*pt::this_worker, std::forward<P>(predicate));
}

// Procedure: _corun_graph //  是这个缩写吗 dysNote std::coroutine_handle cpp20携程 协同程序
template <typename I>
void Executor::_corun_graph(Worker& w, Node* p, I first, I last) {

  // empty graph
  if(first == last) {
    return;
  }
  
  // anchor this parent as the blocking point
  {
    AnchorGuard anchor(p);
    _schedule_graph_with_parent(w, first, last, p);
    _corun_until(w, [p] () -> bool { 
      return p->_join_counter.load(std::memory_order_acquire) == 0; }
    );
  }

  // rethrow the exception to the blocker
  p->_rethrow_exception();
}

// Procedure: _increment_topology
inline void Executor::_increment_topology() {
#if __cplusplus >= TF_CPP20
  _num_topologies.fetch_add(1, std::memory_order_relaxed);
#else
  std::lock_guard<std::mutex> lock(_topology_mutex);
  ++_num_topologies;
#endif
}

// Procedure: _decrement_topology
inline void Executor::_decrement_topology() {
#if __cplusplus >= TF_CPP20
  if(_num_topologies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    _num_topologies.notify_all();
  }
#else
  std::lock_guard<std::mutex> lock(_topology_mutex);
  if(--_num_topologies == 0) {
    _topology_cv.notify_all();
  }
#endif
}

// Procedure: wait_for_all
inline void Executor::wait_for_all() {
#if __cplusplus >= TF_CPP20
  size_t n = _num_topologies.load(std::memory_order_acquire);
  while(n != 0) {
    _num_topologies.wait(n, std::memory_order_acquire);
    n = _num_topologies.load(std::memory_order_acquire);
  }
#else
  std::unique_lock<std::mutex> lock(_topology_mutex);
  _topology_cv.wait(lock, [&](){ return _num_topologies == 0; });
#endif
}

// Function: _set_up_topology
inline void Executor::_set_up_topology(Worker* w, Topology* tpg) {

  // ---- under taskflow lock ----
  auto& g = tpg->_taskflow._graph;
  
  auto send = _set_up_graph(g.begin(), g.end(), tpg, nullptr);
  tpg->_join_counter.store(send - g.begin(), std::memory_order_relaxed);

  w ? _schedule(*w, g.begin(), send) : _schedule(g.begin(), send); //一开始主线程 没有worker 只会执后面这个, 将拓扑的起始节点加入本地就绪队列 
}

// Function: _set_up_graph
template <typename I>
I Executor::_set_up_graph(I first, I last, Topology* tpg, Node* parent) {
    // dysNote 注 task. emplace的时候只塞了Node的部分字段。 其他topology 在这里塞
  auto send = first;
  for(; first != last; ++first) {

    auto node = first->get();
    node->_topology = tpg;
    node->_parent = parent;
    node->_nstate = NSTATE::NONE;
    node->_estate.store(ESTATE::NONE, std::memory_order_relaxed);
    node->_set_up_join_counter();
    node->_exception_ptr = nullptr;

    // move source to the first partition
    // root, root, root, v1, v2, v3, v4, ...
    if(node->num_predecessors() == 0) { //如果没有前驱结点 移到最前面
      std::iter_swap(send++, first);
    }
  }
  return send;
}

// Function: _tear_down_topology
inline void Executor::_tear_down_topology(Worker& worker, Topology* tpg) {

  auto &f = tpg->_taskflow;

  //assert(&tpg == &(f._topologies.front()));

  // case 1: we still need to run the topology again
  if(!tpg->_exception_ptr && !tpg->cancelled() && !tpg->_pred()) {
    //assert(tpg->_join_counter == 0);
    std::lock_guard<std::mutex> lock(f._mutex);
    _set_up_topology(&worker, tpg);
  }
  // case 2: the final run of this topology
  else {

    // invoke the callback after each run
    if(tpg->_call != nullptr) {
      tpg->_call();
    }

    // If there is another run (interleave between lock)
    if(std::unique_lock<std::mutex> lock(f._mutex); f._topologies.size()>1) {
      //assert(tpg->_join_counter == 0);

      // Set the promise
      tpg->_promise.set_value();
      f._topologies.pop();
      tpg = f._topologies.front().get();

      // decrement the topology
      _decrement_topology();

      // set up topology needs to be under the lock or it can
      // introduce memory order error with pop
      _set_up_topology(&worker, tpg);
    }
    else {
      //assert(f._topologies.size() == 1);

      auto fetched_tpg {std::move(f._topologies.front())};
      f._topologies.pop();
      auto satellite {f._satellite};

      lock.unlock();
      
      // Soon after we carry out the promise, there is no longer any guarantee
      // for the lifetime of the associated taskflow.
      fetched_tpg->_carry_out_promise();

      _decrement_topology();

      // remove the taskflow if it is managed by the executor
      // TODO: in the future, we may need to synchronize on wait
      // (which means the following code should the moved before set_value)
      if(satellite) {
        std::scoped_lock<std::mutex> satellite_lock(_taskflows_mutex);
        _taskflows.erase(*satellite);
      }
    }
  }
}

// ############################################################################
// Forward Declaration: Subflow
// ############################################################################

inline void Subflow::join() {

  if(!joinable()) {
    TF_THROW("subflow already joined");
  }
    
  _executor._corun_graph(_worker, _parent, _graph.begin(), _graph.end());
  
  // join here since corun graph may throw exception
  _parent->_nstate |= NSTATE::JOINED_SUBFLOW;
}

#endif




}  // end of namespace tf -----------------------------------------------------

