#pragma once

#include "../utility/macros.hpp"
#include "../utility/traits.hpp"

/**
@file tsq.hpp
@brief task queue include file
*/

#ifndef TF_DEFAULT_BOUNDED_TASK_QUEUE_LOG_SIZE 
  /**
  @def TF_DEFAULT_BOUNDED_TASK_QUEUE_LOG_SIZE
  
  This macro defines the default size of the bounded task queue in Log2. 
  Bounded task queue is used by each worker.
  */
  #define TF_DEFAULT_BOUNDED_TASK_QUEUE_LOG_SIZE 8
#endif

#ifndef TF_DEFAULT_UNBOUNDED_TASK_QUEUE_LOG_SIZE 
  /**
  @def TF_DEFAULT_UNBOUNDED_TASK_QUEUE_LOG_SIZE
  
  This macro defines the default size of the unbounded task queue in Log2.
  Unbounded task queue is used by the executor.
  */
  #define TF_DEFAULT_UNBOUNDED_TASK_QUEUE_LOG_SIZE 10
#endif

namespace tf {

// ----------------------------------------------------------------------------
// Task Queue
// ----------------------------------------------------------------------------


/**
@class: UnboundedTaskQueue

@tparam T data type (must be a pointer type)

@brief class to create a lock-free unbounded work-stealing queue

This class implements the work-stealing queue described in the paper,
<a href="https://www.di.ens.fr/~zappa/readings/ppopp13.pdf">Correct and Efficient Work-Stealing for Weak Memory Models</a>.

Only the queue owner can perform pop and push operations,
while others can steal data from the queue simultaneously.

*/
template <typename T>
class UnboundedTaskQueue {
  
  static_assert(std::is_pointer_v<T>, "T must be a pointer type");

  struct Array {

    int64_t C; //容量
    int64_t M; // 掩码
    std::atomic<T>* S;

    explicit Array(int64_t c) :
      C {c},
      M {c-1},
      S {new std::atomic<T>[static_cast<size_t>(C)]} {
    }
    // 环形缓存的实现 用16 mask 16-1 0x1111这样比 % 实现快

    ~Array() {
      delete [] S;
    }

    int64_t capacity() const noexcept {
      return C;
    }

    void push(int64_t i, T o) noexcept {
      S[i & M].store(o, std::memory_order_relaxed);
    }

    T pop(int64_t i) noexcept {
      return S[i & M].load(std::memory_order_relaxed); // dysNote这里如果T是int 或者T , 不要真的删除或置0 ，因为push会覆盖
    }

    // new一个更大的
    Array* resize(int64_t b, int64_t t) {
      Array* ptr = new Array {2*C};
      for(int64_t i=t; i!=b; ++i) {
        ptr->push(i, pop(i));
      }
      return ptr;
    }

  };

  // Doubling the alignment by 2 seems to generate the most
  // decent performance.
  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _top;
  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _bottom;
  std::atomic<Array*> _array;
  std::vector<Array*> _garbage;

  public:

  /**
  @brief constructs the queue with the given size in the base-2 logarithm

  @param LogSize the base-2 logarithm of the queue size
  */
  explicit UnboundedTaskQueue(int64_t LogSize = TF_DEFAULT_UNBOUNDED_TASK_QUEUE_LOG_SIZE);

  /**
  @brief destructs the queue
  */
  ~UnboundedTaskQueue();

  /**
  @brief queries if the queue is empty at the time of this call
  */
  bool empty() const noexcept;

  /**
  @brief queries the number of items at the time of this call
  */
  size_t size() const noexcept;

  /**
  @brief queries the capacity of the queue
  */
  int64_t capacity() const noexcept;
  
  /**
  @brief inserts an item to the queue

  @param item the item to push to the queue
  
  Only the owner thread can insert an item to the queue.
  The operation can trigger the queue to resize its capacity
  if more space is required.
  */
  void push(T item);

  /**
  @brief pops out an item from the queue

  Only the owner thread can pop out an item from the queue.
  The return can be a @c nullptr if this operation failed (empty queue).
  */
  T pop();

  /**
  @brief steals an item from the queue

  Any threads can try to steal an item from the queue.
  The return can be a @c nullptr if this operation failed (not necessary empty).
  */
  T steal();

  /**
  @brief attempts to steal a task with a hint mechanism
  
  @param num_empty_steals a reference to a counter tracking consecutive empty steal attempts
  
  This function tries to steal a task from the queue. If the steal attempt
  is successful, the stolen task is returned. 
  Additionally, if the queue is empty, the provided counter `num_empty_steals` is incremented;
  otherwise, `num_empty_steals` is reset to zero.

  */
  T steal_with_hint(size_t& num_empty_steals);

  private:

  Array* resize_array(Array* a, int64_t b, int64_t t);
};

// Constructor
template <typename T>
UnboundedTaskQueue<T>::UnboundedTaskQueue(int64_t LogSize) {
  _top.store(0, std::memory_order_relaxed);
  _bottom.store(0, std::memory_order_relaxed);
  _array.store(new Array{(int64_t{1} << LogSize)}, std::memory_order_relaxed);
  _garbage.reserve(32);
}

// Destructor
template <typename T>
UnboundedTaskQueue<T>::~UnboundedTaskQueue() {
  for(auto a : _garbage) {
    delete a;
  }
  delete _array.load(); //获取指针 析构
}

// Function: empty
template <typename T>
bool UnboundedTaskQueue<T>::empty() const noexcept {
  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_relaxed);
  return (b <= t);
}

// Function: size
template <typename T>
size_t UnboundedTaskQueue<T>::size() const noexcept {
  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_relaxed);
  return static_cast<size_t>(b >= t ? b - t : 0);
}

// Function: push
template <typename T>
void UnboundedTaskQueue<T>::push(T o) {

  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_acquire);  // dysNote注意这里为acquire 后面的不能放到前面 releas是前面的不能放到后面 写不能放到后面
  Array* a = _array.load(std::memory_order_relaxed); // 这里是读， 读不前

  // queue is full with one additional item (b-t+1)
  if TF_UNLIKELY(a->capacity() - 1 < (b - t)) {
    a = resize_array(a, b, t);
  }

  a->push(b, o);
  std::atomic_thread_fence(std::memory_order_release);  // a->push不能放到后面

  // original paper uses relaxed here but tsa complains
  _bottom.store(b + 1, std::memory_order_release);
}

// Function: pop
template <typename T>
T UnboundedTaskQueue<T>::pop() {

  int64_t b = _bottom.load(std::memory_order_relaxed) - 1;
  Array* a = _array.load(std::memory_order_relaxed);
  _bottom.store(b, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t t = _top.load(std::memory_order_relaxed);

  T item {nullptr};

  if(t <= b) {
    item = a->pop(b);
    if(t == b) {
      // the last item just got stolen
      if(!_top.compare_exchange_strong(t, t+1,
                                               std::memory_order_seq_cst,
                                               std::memory_order_relaxed)) {
        item = nullptr;
      }
      _bottom.store(b + 1, std::memory_order_relaxed);
    }
    // dysNote 这里 一个队列  1|2|3|4|5|6 注意这里Array里面并不会真的pop出去，而是通过push覆盖的逻辑
    // 所以这里 如果top = 2 , bottom是4的时候， bottom是待插入点。 看代码逻辑 .bottom- 1才是真的最后一个元素
    // 如果 top = 2 bottom = 3 这个时候 最后一个元素是bottom = 1 是2 。所以top = bottom - 1
    // 这个时候bottom继续 _bottom.store(b + 1)  是3， 然后2这个top其实Array里是有值的。然后将top +1 = 3 ,至于
    // 第二个位置等待push将他覆盖
  }
  else {
    _bottom.store(b + 1, std::memory_order_relaxed);
  }

  return item;
  // dysNote CAS的基本形式是：CAS(addr,old,new),当addr中存放的值等于old时，用new对其替换
  // C++ style compare_exchange_strong (T& expected, T val, memory_order sync = memory_order_seq_cst)
  // memory_order_acq_rel只能限制与原子操作相关的变量 。限制不了非原子操作的变量
}

// Function: steal
template <typename T>
T UnboundedTaskQueue<T>::steal() { // 上面那个pop是从_bottom pop掉。这个是从_top中pop    我靠我还以为是个栈
  
  int64_t t = _top.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t b = _bottom.load(std::memory_order_acquire);

  T item {nullptr};

  if(t < b) {
    Array* a = _array.load(std::memory_order_consume); // 等价与accquire 也是后面的不能放到前面  但是比accquire好的是 只限制了与该变量相关的不得前
    item = a->pop(t);
    if(!_top.compare_exchange_strong(t, t+1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
  }

  return item;
}

// Function: steal
template <typename T>
T UnboundedTaskQueue<T>::steal_with_hint(size_t& num_empty_steals) {
  
  int64_t t = _top.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t b = _bottom.load(std::memory_order_acquire);

  T item {nullptr};

  if(t < b) {
    num_empty_steals = 0;
    Array* a = _array.load(std::memory_order_consume);
    item = a->pop(t);
    if(!_top.compare_exchange_strong(t, t+1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
  }
  ++num_empty_steals;
  return item;
}

// Function: capacity
template <typename T>
int64_t UnboundedTaskQueue<T>::capacity() const noexcept {
  return _array.load(std::memory_order_relaxed)->capacity();
}

template <typename T>
typename UnboundedTaskQueue<T>::Array*
UnboundedTaskQueue<T>::resize_array(Array* a, int64_t b, int64_t t) {

  //Array* tmp = a->resize(b, t);
  //_garbage.push_back(a);
  //std::swap(a, tmp);
  //_array.store(a, std::memory_order_release);
  //// Note: the original paper using relaxed causes t-san to complain
  ////_array.store(a, std::memory_order_relaxed);
  //return a;
  

  Array* tmp = a->resize(b, t);
  _garbage.push_back(a);
  _array.store(tmp, std::memory_order_release);
  // Note: the original paper using relaxed causes t-san to complain
  //_array.store(a, std::memory_order_relaxed);
  return tmp;
}

// ----------------------------------------------------------------------------
// BoundedTaskQueue
// ----------------------------------------------------------------------------

/**
@class: BoundedTaskQueue

@tparam T data type
@tparam LogSize the base-2 logarithm of the queue size

@brief class to create a lock-free bounded work-stealing queue

This class implements the work-stealing queue described in the paper, 
"Correct and Efficient Work-Stealing for Weak Memory Models,"
available at https://www.di.ens.fr/~zappa/readings/ppopp13.pdf.

Only the queue owner can perform pop and push operations,
while others can steal data from the queue.
*/
template <typename T, size_t LogSize = TF_DEFAULT_BOUNDED_TASK_QUEUE_LOG_SIZE>
class BoundedTaskQueue {
  
  static_assert(std::is_pointer_v<T>, "T must be a pointer type");
  
  constexpr static int64_t BufferSize = int64_t{1} << LogSize;
  constexpr static int64_t BufferMask = (BufferSize - 1);

  static_assert((BufferSize >= 2) && ((BufferSize & (BufferSize - 1)) == 0));

  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _top {0};
  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _bottom {0};
  alignas(2*TF_CACHELINE_SIZE) std::atomic<T> _buffer[BufferSize];

  public:
    
  /**
  @brief constructs the queue with a given capacity
  */
  BoundedTaskQueue() = default;

  /**
  @brief destructs the queue
  */
  ~BoundedTaskQueue() = default;
  
  /**
  @brief queries if the queue is empty at the time of this call
  */
  bool empty() const noexcept;
  
  /**
  @brief queries the number of items at the time of this call
  */
  size_t size() const noexcept;

  /**
  @brief queries the capacity of the queue
  */
  constexpr size_t capacity() const;
  
  /**
  @brief tries to insert an item to the queue

  @tparam O data type 
  @param item the item to perfect-forward to the queue
  @return `true` if the insertion succeed or `false` (queue is full)
  
  Only the owner thread can insert an item to the queue. 

  */
  template <typename O>
  bool try_push(O&& item);
  
  /**
  @brief tries to insert an item to the queue or invoke the callable if fails

  @tparam O data type 
  @tparam C callable type
  @param item the item to perfect-forward to the queue
  @param on_full callable to invoke when the queue is full (insertion fails)
  
  Only the owner thread can insert an item to the queue. 

  */
  template <typename O, typename C>
  void push(O&& item, C&& on_full);
  
  /**
  @brief pops out an item from the queue

  Only the owner thread can pop out an item from the queue. 
  The return can be a `nullptr` if this operation failed (empty queue).
  */
  T pop();
  
  /**
  @brief steals an item from the queue

  Any threads can try to steal an item from the queue.
  The return can be a `nullptr` if this operation failed (not necessary empty).
  */
  T steal();

  /**
  @brief attempts to steal a task with a hint mechanism
  
  @param num_empty_steals a reference to a counter tracking consecutive empty steal attempts
  
  This function tries to steal a task from the queue. If the steal attempt
  is successful, the stolen task is returned. 
  Additionally, if the queue is empty, the provided counter `num_empty_steals` is incremented;
  otherwise, `num_empty_steals` is reset to zero.
  */
  T steal_with_hint(size_t& num_empty_steals);
};

// Function: empty
template <typename T, size_t LogSize>
bool BoundedTaskQueue<T, LogSize>::empty() const noexcept {
  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_relaxed);
  return b <= t;
}

// Function: size
template <typename T, size_t LogSize>
size_t BoundedTaskQueue<T, LogSize>::size() const noexcept {
  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_relaxed);
  return static_cast<size_t>(b >= t ? b - t : 0);
}

// Function: try_push
template <typename T, size_t LogSize>
template <typename O>
bool BoundedTaskQueue<T, LogSize>::try_push(O&& o) {

  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_acquire);

  // queue is full with one additional item (b-t+1)
  if TF_UNLIKELY((b - t) > BufferSize - 1) {
    return false;
  }
  
  _buffer[b & BufferMask].store(std::forward<O>(o), std::memory_order_relaxed);

  std::atomic_thread_fence(std::memory_order_release);
  
  // original paper uses relaxed here but tsa complains
  _bottom.store(b + 1, std::memory_order_release);

  return true;
}

// Function: push
template <typename T, size_t LogSize>
template <typename O, typename C>
void BoundedTaskQueue<T, LogSize>::push(O&& o, C&& on_full) {

  int64_t b = _bottom.load(std::memory_order_relaxed);
  int64_t t = _top.load(std::memory_order_acquire);

  // queue is full with one additional item (b-t+1)
  if TF_UNLIKELY((b - t) > BufferSize - 1) {
    on_full();
    return;
  }
  
  _buffer[b & BufferMask].store(std::forward<O>(o), std::memory_order_relaxed);

  std::atomic_thread_fence(std::memory_order_release);
  
  // original paper uses relaxed here but tsa complains
  _bottom.store(b + 1, std::memory_order_release);
}

// Function: pop
template <typename T, size_t LogSize>
T BoundedTaskQueue<T, LogSize>::pop() {

  int64_t b = _bottom.load(std::memory_order_relaxed) - 1;
  _bottom.store(b, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t t = _top.load(std::memory_order_relaxed);

  T item {nullptr};

  if(t <= b) {
    item = _buffer[b & BufferMask].load(std::memory_order_relaxed);
    if(t == b) {
      // the last item just got stolen
      if(!_top.compare_exchange_strong(t, t+1, 
                                       std::memory_order_seq_cst, 
                                       std::memory_order_relaxed)) {
        item = nullptr;
      }
      _bottom.store(b + 1, std::memory_order_relaxed);
    }
  }
  else {
    _bottom.store(b + 1, std::memory_order_relaxed);
  }
  /* 为什么不用while 和weak呢 
   void NamingCache::DelayReleaseNode(Node* node) {
      auto* head = delayed_release_node_list_.load(std::memory_order_relaxed);
      node->next = head;
      while (!delayed_release_node_list_.compare_exchange_weak(
          node->next, node, std::memory_order_release, std::memory_order_acquire))
        ;
        // 如果CAS失败，说明有其他线程在这个位置插入了新的节点 oldValue会变成新的head
        // Note这种写法是CAS(node->next, node)
    }
    void pop(T& result) {
        node* old_head=head.load();
        while(!head.compare_exchange_weak(old_head,old_head->next));
        result=old_head->data;
    }
   * */

  return item;
}

// Function: steal
template <typename T, size_t LogSize>
T BoundedTaskQueue<T, LogSize>::steal() {
  int64_t t = _top.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t b = _bottom.load(std::memory_order_acquire);
  
  T item{nullptr};

  if(t < b) {
    item = _buffer[t & BufferMask].load(std::memory_order_relaxed);
    if(!_top.compare_exchange_strong(t, t+1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
  }

  return item;
}

// Function: steal
template <typename T, size_t LogSize>
T BoundedTaskQueue<T, LogSize>::steal_with_hint(size_t& num_empty_steals) {
  int64_t t = _top.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  int64_t b = _bottom.load(std::memory_order_acquire);
  
  T item {nullptr};

  if(t < b) {
    num_empty_steals = 0;
    item = _buffer[t & BufferMask].load(std::memory_order_relaxed);
    if(!_top.compare_exchange_strong(t, t+1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
  }
  ++num_empty_steals;
  return item;
}

// Function: capacity
template <typename T, size_t LogSize>
constexpr size_t BoundedTaskQueue<T, LogSize>::capacity() const {
  return static_cast<size_t>(BufferSize);
}



//-----------------------------------------------------------------------------

//template <typename T>
//class UnboundedTaskQueue2 {
//  
//  static_assert(std::is_pointer_v<T>, "T must be a pointer type");
//
//  struct Array {
//
//    int64_t C;
//    int64_t M;
//    std::atomic<T>* S;
//
//    explicit Array(int64_t c) :
//      C {c},
//      M {c-1},
//      S {new std::atomic<T>[static_cast<size_t>(C)]} {
//    }
//
//    ~Array() {
//      delete [] S;
//    }
//
//    int64_t capacity() const noexcept {
//      return C;
//    }
//
//    void push(int64_t i, T o) noexcept {
//      S[i & M].store(o, std::memory_order_relaxed);
//    }
//
//    T pop(int64_t i) noexcept {
//      return S[i & M].load(std::memory_order_relaxed);
//    }
//
//    Array* resize(int64_t b, int64_t t) {
//      Array* ptr = new Array {2*C};
//      for(int64_t i=t; i!=b; ++i) {
//        ptr->push(i, pop(i));
//      }
//      return ptr;
//    }
//
//  };
//
//  // Doubling the alignment by 2 seems to generate the most
//  // decent performance.
//  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _top;
//  alignas(2*TF_CACHELINE_SIZE) std::atomic<int64_t> _bottom;
//  std::atomic<Array*> _array;
//  std::vector<Array*> _garbage;
//
//  static constexpr int64_t BOTTOM_LOCK = std::numeric_limits<int64_t>::min();
//  static constexpr int64_t BOTTOM_MASK = std::numeric_limits<int64_t>::max();
//
//  public:
//
//  /**
//  @brief constructs the queue with the given size in the base-2 logarithm
//
//  @param LogSize the base-2 logarithm of the queue size
//  */
//  explicit UnboundedTaskQueue2(int64_t LogSize = TF_DEFAULT_UNBOUNDED_TASK_QUEUE_LOG_SIZE);
//
//  /**
//  @brief destructs the queue
//  */
//  ~UnboundedTaskQueue2();
//
//  /**
//  @brief queries if the queue is empty at the time of this call
//  */
//  bool empty() const noexcept;
//
//  /**
//  @brief queries the number of items at the time of this call
//  */
//  size_t size() const noexcept;
//
//  /**
//  @brief queries the capacity of the queue
//  */
//  int64_t capacity() const noexcept;
//  
//  /**
//  @brief inserts an item to the queue
//
//  @param item the item to push to the queue
//  
//  Only the owner thread can insert an item to the queue.
//  The operation can trigger the queue to resize its capacity
//  if more space is required.
//  */
//  void push(T item);
//
//  /**
//  @brief steals an item from the queue
//
//  Any threads can try to steal an item from the queue.
//  The return can be a @c nullptr if this operation failed (not necessary empty).
//  */
//  T steal();
//
//  private:
//
//  Array* resize_array(Array* a, int64_t b, int64_t t);
//};
//
//// Constructor
//template <typename T>
//UnboundedTaskQueue2<T>::UnboundedTaskQueue2(int64_t LogSize) {
//  _top.store(0, std::memory_order_relaxed);
//  _bottom.store(0, std::memory_order_relaxed);
//  _array.store(new Array{(int64_t{1} << LogSize)}, std::memory_order_relaxed);
//  _garbage.reserve(32);
//}
//
//// Destructor
//template <typename T>
//UnboundedTaskQueue2<T>::~UnboundedTaskQueue2() {
//  for(auto a : _garbage) {
//    delete a;
//  }
//  delete _array.load();
//}
//
//// Function: empty
//template <typename T>
//bool UnboundedTaskQueue2<T>::empty() const noexcept {
//  int64_t b = _bottom.load(std::memory_order_relaxed) & BOTTOM_MASK;
//  int64_t t = _top.load(std::memory_order_relaxed);
//  return (b <= t);
//}
//
//// Function: size
//template <typename T>
//size_t UnboundedTaskQueue2<T>::size() const noexcept {
//  int64_t b = _bottom.load(std::memory_order_relaxed) & BOTTOM_MASK;
//  int64_t t = _top.load(std::memory_order_relaxed);
//  return static_cast<size_t>(b >= t ? b - t : 0);
//}
//
//// Function: push
//template <typename T>
//void UnboundedTaskQueue2<T>::push(T o) {
//  
//  // spin until getting an exclusive access to b
//  int64_t b = _bottom.load(std::memory_order_acquire) & BOTTOM_MASK;
//  while(!_bottom.compare_exchange_weak(b, b | BOTTOM_LOCK, std::memory_order_acquire,
//                                                           std::memory_order_relaxed)) {
//    b = b & BOTTOM_MASK;
//  }
//
//  // critical region
//  int64_t t = _top.load(std::memory_order_acquire);
//  Array* a = _array.load(std::memory_order_relaxed);
//
//  // queue is full
//  if TF_UNLIKELY(a->capacity() - 1 < (b - t)) {
//    a = resize_array(a, b, t);
//  }
//
//  a->push(b, o);
//  std::atomic_thread_fence(std::memory_order_release);
//
//  // original paper uses relaxed here but tsa complains
//  _bottom.store(b + 1, std::memory_order_release);
//}
//
//// Function: steal
//template <typename T>
//T UnboundedTaskQueue2<T>::steal() {
//  
//  int64_t t = _top.load(std::memory_order_acquire);
//  std::atomic_thread_fence(std::memory_order_seq_cst);
//  int64_t b = _bottom.load(std::memory_order_acquire) & BOTTOM_MASK;
//
//  T item {nullptr};
//
//  if(t < b) {
//    Array* a = _array.load(std::memory_order_consume);
//    item = a->pop(t);
//    if(!_top.compare_exchange_strong(t, t+1,
//                                     std::memory_order_seq_cst,
//                                     std::memory_order_relaxed)) {
//      return nullptr;
//    }
//  }
//
//  return item;
//}
//
//// Function: capacity
//template <typename T>
//int64_t UnboundedTaskQueue2<T>::capacity() const noexcept {
//  return _array.load(std::memory_order_relaxed)->capacity();
//}
//
//template <typename T>
//typename UnboundedTaskQueue2<T>::Array*
//UnboundedTaskQueue2<T>::resize_array(Array* a, int64_t b, int64_t t) {
//
//  Array* tmp = a->resize(b, t);
//  _garbage.push_back(a);
//  std::swap(a, tmp);
//  _array.store(a, std::memory_order_release);
//  // Note: the original paper using relaxed causes t-san to complain
//  //_array.store(a, std::memory_order_relaxed);
//  return a;
//}

}  // end of namespace tf -----------------------------------------------------



/*
  template<typename T>
class BoundedBlockingQueue : boost::noncopyable
{
public:
  	explicit BoundedBlockingQueue(int maxSize)
    	: mutex_(),
		notEmpty_(mutex_),
		notFull_(mutex_),
		queue_(maxSize)
	{
	}

	void put(const T& x)
	{
		MutexLockGuard lock(mutex_);
		while (queue_.full())
		{
		  	notFull_.wait();
		}
		assert(!queue_.full());
		queue_.push_back(x);
		notEmpty_.notify(); // TODO: move outside of lock
  	}

	T take()
	{
		MutexLockGuard lock(mutex_);
		while (queue_.empty())
		{
			notEmpty_.wait();
		}
		assert(!queue_.empty());
		T front(queue_.front());
		queue_.pop_front();
		notFull_.notify(); // TODO: move outside of lock
		return front;
  	}

	bool empty() const
	{
		MutexLockGuard lock(mutex_);
		return queue_.empty();
  	}

	bool full() const
  	{
		MutexLockGuard lock(mutex_);
		return queue_.full();
  	}

  	size_t size() const
  	{
		MutexLockGuard lock(mutex_);
		return queue_.size();
  	}

  	size_t capacity() const
  	{
		MutexLockGuard lock(mutex_);
		return queue_.capacity();
  	}

private:
	mutable MutexLock          mutex_;
	//这两个条件变量是同一个mutex
	Condition                  notEmpty_;
	Condition                  notFull_;
	boost::circular_buffer<T>  queue_;
};

 */
