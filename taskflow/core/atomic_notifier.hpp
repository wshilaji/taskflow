#if __cplusplus >= TF_CPP20

#pragma once

#include <atomic>
#include <thread>
#include <vector>

namespace tf {

class AtomicNotifierV1 {

  friend class Executor;

  public:
  
  struct Waiter {
    alignas (2*TF_CACHELINE_SIZE) uint32_t epoch;
  };

  AtomicNotifierV1(size_t N) noexcept : _state(0), _waiters(N) {}
  ~AtomicNotifierV1() { assert((_state.load() & WAITER_MASK) == 0); } 

  void notify_one() noexcept;
  void notify_all() noexcept;
  void notify_n(size_t n) noexcept;
  void prepare_wait(Waiter*) noexcept;
  void cancel_wait(Waiter*) noexcept;
  void commit_wait(Waiter*) noexcept;
  size_t size() const noexcept;

 private:

  AtomicNotifierV1(const AtomicNotifierV1&) = delete;
  AtomicNotifierV1(AtomicNotifierV1&&) = delete;
  AtomicNotifierV1& operator=(const AtomicNotifierV1&) = delete;
  AtomicNotifierV1& operator=(AtomicNotifierV1&&) = delete;

  // This requires 64-bit
  static_assert(sizeof(int) == 4, "bad platform");
  static_assert(sizeof(uint32_t) == 4, "bad platform");
  static_assert(sizeof(uint64_t) == 8, "bad platform");
  static_assert(sizeof(std::atomic<uint64_t>) == 8, "bad platform");

  // _state stores the epoch in the most significant 32 bits and the
  // waiter count in the least significant 32 bits.
  std::atomic<uint64_t> _state;
  std::vector<Waiter> _waiters;

  static constexpr uint64_t WAITER_INC  {1};
  static constexpr size_t   EPOCH_SHIFT {32};
  static constexpr uint64_t EPOCH_INC   {uint64_t(1) << EPOCH_SHIFT};
  static constexpr uint64_t WAITER_MASK {EPOCH_INC - 1};
};

inline void AtomicNotifierV1::notify_one() noexcept {
  uint64_t prev = _state.fetch_add(EPOCH_INC, std::memory_order_acq_rel);
  if(TF_UNLIKELY(prev & WAITER_MASK))  { // has waiter (typically unlikely)
    _state.notify_one();
  }
}

inline void AtomicNotifierV1::notify_all() noexcept {
    // fetch_add 方法返回旧的值
  uint64_t prev = _state.fetch_add(EPOCH_INC, std::memory_order_acq_rel);
  if(TF_UNLIKELY(prev & WAITER_MASK))  { // has waiter (typically unlikely)
    _state.notify_all();
  }
}
  
inline void AtomicNotifierV1::notify_n(size_t n) noexcept {
  if(n >= _waiters.size()) {
    notify_all();
  }
  else {
    for(size_t k=0; k<n; ++k) {
      notify_one();
    }
  }
}

inline size_t AtomicNotifierV1::size() const noexcept {
  return _waiters.size();
}

inline void AtomicNotifierV1::prepare_wait(Waiter* waiter) noexcept {
  uint64_t prev = _state.fetch_add(WAITER_INC, std::memory_order_acq_rel);
  waiter->epoch = (prev >> EPOCH_SHIFT);
}

inline void AtomicNotifierV1::cancel_wait(Waiter*) noexcept {
  // memory_order_relaxed would suffice for correctness, but the faster
  // #waiters gets to 0, the less likely it is that we'll do spurious wakeups
  // (and thus system calls).
  _state.fetch_sub(WAITER_INC, std::memory_order_seq_cst);
}
// 如果线程A B C, AB先commit 然后主线程notify  ,. C在commit 就会一直等待吧1 
inline void AtomicNotifierV1::commit_wait(Waiter* waiter) noexcept {
  uint64_t prev = _state.load(std::memory_order_acquire);
  while((prev >> EPOCH_SHIFT) == waiter->epoch) {
      // 当state 与prev不同时唤醒   cv.wait(lock) 
    _state.wait(prev, std::memory_order_acquire); //dysNote 原子等待cpp20新特性. wait(prev)当prev发生变化唤醒 不会自动唤醒等待的线程 
    prev = _state.load(std::memory_order_acquire);
  }
  // while(!ready) {cv.wait(lock),}
  // memory_order_relaxed would suffice for correctness, but the faster
  // #waiters gets to 0, the less likely it is that we'll do spurious wakeups
  // (and thus system calls)
  _state.fetch_sub(WAITER_INC, std::memory_order_seq_cst);
}
//该循环检查当前的状态是否与等待者的纪元（epoch）相同。如果相同，表示等待者需要继续等待。
//调用 _state.wait(prev, std::memory_order_acquire) 将当前线程阻塞，直到 _state 的值发生变化。
///一旦被唤醒，重新加载 _state 的值以获取最新状态。

//-----------------------------------------------------------------------------

class AtomicNotifierV2 {

  friend class Executor;

  public:
  
  struct Waiter {
    alignas (2*TF_CACHELINE_SIZE) uint32_t epoch;
  };

  AtomicNotifierV2(size_t N) noexcept : _state(0), _waiters(N) {}
  ~AtomicNotifierV2() { assert((_state.load() & WAITER_MASK) == 0); } 

  void notify_one() noexcept;
  void notify_all() noexcept;
  void notify_n(size_t n) noexcept;
  void prepare_wait(Waiter*) noexcept;
  void cancel_wait(Waiter*) noexcept;
  void commit_wait(Waiter*) noexcept;
  size_t size() const noexcept;

 private:

  AtomicNotifierV2(const AtomicNotifierV2&) = delete;
  AtomicNotifierV2(AtomicNotifierV2&&) = delete;
  AtomicNotifierV2& operator=(const AtomicNotifierV2&) = delete;
  AtomicNotifierV2& operator=(AtomicNotifierV2&&) = delete;

  // This requires 64-bit
  static_assert(sizeof(int) == 4, "bad platform");
  static_assert(sizeof(uint32_t) == 4, "bad platform");
  static_assert(sizeof(uint64_t) == 8, "bad platform");
  static_assert(sizeof(std::atomic<uint64_t>) == 8, "bad platform");

  // _state stores the epoch in the most significant 32 bits and the
  // waiter count in the least significant 32 bits.
  std::atomic<uint64_t> _state;
  std::vector<Waiter> _waiters;

  static constexpr uint64_t WAITER_INC  {1};
  static constexpr uint64_t EPOCH_SHIFT {32};
  static constexpr uint64_t EPOCH_INC   {uint64_t(1) << EPOCH_SHIFT};
  static constexpr uint64_t WAITER_MASK {EPOCH_INC - 1};
};

inline void AtomicNotifierV2::notify_one() noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  //if((_state.load(std::memory_order_acquire) & WAITER_MASK) != 0) {
  //  _state.fetch_add(EPOCH_INC, std::memory_order_relaxed);
  //  _state.notify_one(); 
  //}

  for(uint64_t state = _state.load(std::memory_order_acquire); state & WAITER_MASK;) {
    if(_state.compare_exchange_weak(state, state + EPOCH_INC, std::memory_order_acquire)) {
      _state.notify_one(); 
      break;
    }
  }
}

inline void AtomicNotifierV2::notify_all() noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  //if((_state.load(std::memory_order_acquire) & WAITER_MASK) != 0) {
  //  _state.fetch_add(EPOCH_INC, std::memory_order_relaxed);
  //  _state.notify_all(); 
  //}
  for(uint64_t state = _state.load(std::memory_order_acquire); state & WAITER_MASK;) {
    if(_state.compare_exchange_weak(state, state + EPOCH_INC, std::memory_order_acquire)) {
      _state.notify_all(); 
      break;
    }
  }
}
  
inline void AtomicNotifierV2::notify_n(size_t n) noexcept {
  if(n >= _waiters.size()) {
    notify_all();
  }
  else {
    for(size_t k=0; k<n; ++k) {
      notify_one();
    }
  }
}

inline size_t AtomicNotifierV2::size() const noexcept {
  return _waiters.size();
}

inline void AtomicNotifierV2::prepare_wait(Waiter* waiter) noexcept {
  auto prev = _state.fetch_add(WAITER_INC, std::memory_order_relaxed);
  waiter->epoch = (prev >> EPOCH_SHIFT);
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline void AtomicNotifierV2::cancel_wait(Waiter*) noexcept {
  _state.fetch_sub(WAITER_INC, std::memory_order_seq_cst);
}

inline void AtomicNotifierV2::commit_wait(Waiter* waiter) noexcept {
  uint64_t prev = _state.load(std::memory_order_acquire);
  while((prev >> EPOCH_SHIFT) == waiter->epoch) {
    _state.wait(prev, std::memory_order_acquire); 
    prev = _state.load(std::memory_order_acquire);
  }
  // memory_order_relaxed would suffice for correctness, but the faster
  // #waiters gets to 0, the less likely it is that we'll do spurious wakeups
  // (and thus system calls)
  _state.fetch_sub(WAITER_INC, std::memory_order_seq_cst);
}



} // namespace taskflow -------------------------------------------------------

#endif
/*
std::atomic<bool> x = {false};
std::atomic<bool> y = {false};
std::atomic<int> z = {0};
void write_x() {
    x.store(true, std::memory_order_seq_cst);
}
void write_y() {
    y.store(true, std::memory_order_seq_cst);
}
void read_x_then_y() {
    while (!x.load(std::memory_order_seq_cst)) {}
    if (y.load(std::memory_order_seq_cst)) ++z;
}
void read_y_then_x() {
    while (!y.load(std::memory_order_seq_cst)) {}
    if (x.load(std::memory_order_seq_cst)) ++z;
}
// 注意是所有线程看到的  注意所有线程看到的原子操作的的执行顺序是相同的 顺序可能如下 有z = 1或者z = 2的情况 
std::thread a(write_x);
std::thread b(write_y);
std::thread c(read_x_then_y);
std::thread d(read_y_then_x);

如果上面用acquire和release替换了std::memory_order_seq_cst  可能出现的z= 0,  原因是read_x_then_y和read_y_then_x看到的x和y的修改顺序并不一致，也就是说read_x_then_y看到的是先修改了x再修改y, 而read_y_then_x看到的是先修改y再修改x, 这种现象出现的原因仍然可以归结到写操作的传播上，在某一时刻write_x的修改只传播到了read_x_then_y线程所在的processor，还没有传播到read_y_then_x线程所在的processor, 而write_y的修改只传播到了read_y_then_x线程所在的processor，还没有传播到read_x_then_y线程所在的processor.

如果我们要保证最后z一定不等于0，需要将代码中std::memory_order_acquire和std::memory_order_release都换成std::memory_order_seq_cst



C++的六种内存序，我们首先须要明白一点，处理器读取一个数据时，可能从内存中读取，也可能从缓存中读取，还可能从寄存器读取。对于一个写操作，要考虑这个操作的结果传播到其他处理器的速度。
并且，编译器的指令重排和CPU处理器的乱序执行也是我们需要考虑的因素。 // https://www.cnblogs.com/ljmiao/p/18145946 P1 没有等待 Data 的写结果传播到 P2 的缓存中,继续进行 Head 的写操作, Head=1；
一切原因一个是写操作的传播。一个是编译器的指令重排。!!!!
memory_order_relaxed 的要求是在同一线程中，对同一原子变量的访问不可以被重排  不同的原子变量的操作顺序是可以重排的。
int x = 0;
int y = 0;
// Thread 1:
r1 = y.load(std::memory_order_relaxed); // A
x.store(r1, std::memory_order_relaxed); // B
// Thread 2:
r2 = x.load(std::memory_order_relaxed); // C
y.store(42, std::memory_order_relaxed); // D
                                        // 代码执行后，r1=r2=42的情况是可能出现的，因为第8行的代码可以被重排到第7行之前执行。
 * */
