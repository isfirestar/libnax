#if !defined TASK_SCHEDULRE_HEADER_20160707
#define TASK_SCHEDULRE_HEADER_20160707

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <deque>
#include <functional>

#include "os_util.hpp"

/* 一套使用多种线程模型， 支持优先队列的, 模板化的任务系统 */
namespace nsp {
    namespace toolkit {

        enum PAGED_TASK_PRIORITY {
            kLowPagePriority = 0,
            kNormalPagePriority = 16,
            kHighPagePriority = 32,
        };

        template<class T> class task_thread {
            std::atomic<int> join_{-1};
            std::condition_variable cv_;
            std::deque<std::shared_ptr<T>> task_que_;
            std::mutex task_locker_;
            std::thread th_;

            void th_handler() {
                while (join_ < 0) {
                    std::shared_ptr<T> obj = nullptr;
                    {
                        std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                        while (task_que_.empty()) {
                            cv_.wait(guard);
                            if (join_ > 0) return;
                        }
                        obj = std::move(task_que_.front());
                        task_que_.pop_front();
                    }

                    if (obj) obj->on_task();
                }
            }

        public:

            task_thread() : th_(std::bind(&task_thread::th_handler, this)) {
            }

            virtual ~task_thread() {
                join();
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                task_que_.clear();
            }

            void post(const std::shared_ptr<T> &tsk) {
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                task_que_.push_back(tsk);
                cv_.notify_one();
            }

            void join() {
                {
                    std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                    join_ = 1;
                    cv_.notify_one();
                }
                if (th_.joinable()) {
                    th_.join();
                }
            }
        };

        // 带优先级的任务模板

        template<class T> class priority_task {
        public:

            priority_task(const int priority, const std::shared_ptr<T> &task) {
                priority_ = priority;
                task_ = task;
            }

            virtual ~priority_task() {
            }

            bool operator<(const priority_task &rf) const {
                return priority_ < rf.priority_;
            }

            T &reference() {
                return *task_;
            }

            const T &reference() const {
                return *task_;
            }
        private:
            int priority_;
            std::shared_ptr<T> task_;
        };

        // 指针目标对比的仿函数
        template<class T> struct pointer_compatible_compare {
        public:

            bool operator()(const std::shared_ptr<T> &left, const std::shared_ptr<T> &right) const {
                if (!right) {
                    return false;
                }
                if (!left) {
                    return true;
                }
                return left->operator<(*right);
            }

            bool operator()(const T *left, const T *right) const {
                if (!right) {
                    return false;
                }
                if (!left) {
                    return true;
                }
                return *left < *right;
            }
        };

        // 线程模式处理优先队列任务
        template<class T> class priority_task_thread {
            std::priority_queue <priority_task<T> *, std::vector<priority_task<T> *>, pointer_compatible_compare <priority_task<T>>> task_que_;
            std::mutex task_locker_;
            std::condition_variable cv_;
            std::atomic<int> join_{-1};
            std::thread th_;

            void th_handler() {
                while (join_ < 0) {
                    priority_task<T> *ptr = nullptr;
                    {
                        std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                        while (!task_que_.empty()) {
                            cv_.wait(guard);
                            if (join_ > 0) return;
                        }
                        ptr = task_que_.top();
                        task_que_.pop();
                    }
                    if (ptr) {
                        ptr->reference().on_task();
                        delete ptr;
                    }
                }
            }

        public:

            priority_task_thread() : th_(std::bind(&priority_task_thread::th_handler, this)) {
            }

            virtual ~priority_task_thread() {
                join();
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                while (!task_que_.empty()) {
                    priority_task<T> *ptr = task_que_.top();
                    task_que_.pop();
                    delete ptr;
                }
            }

            // 任务优先级可交由调用线程自行指定
            // 如果不指定优先级，则行为和 task_thread 一致， 但由于最小堆和队列的插入开销， 这个类会比 task_thread 效率低
            int post(const std::shared_ptr<T> &tsk, const int priority = 0) {
                try {
                    std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                    priority_task<T> *ptask = new priority_task<T>(priority, tsk);
                    task_que_.push(ptask);
                    cv_.notify_one();
                } catch (...) {
                    return -1;
                }
                return 0;
            }

            void join() {
                {
                    std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                    join_ = 1;
                    cv_.notify_one();
                }

                if (th_.joinable()) {
                    th_.join();
                }
            }
        };

        // 使用线程池的任务模型
        template<class T> class task_thread_pool {
            std::deque<std::shared_ptr<T>> task_que_;
            std::vector<std::thread *> ths_;
            std::mutex task_locker_;
            std::atomic<int> join_{-1};
            std::condition_variable cv_;
            std::recursive_mutex mem_lock_;

            void th_handler() {
                pool_handler();
            }

            virtual void pool_handler() {
                while (join_ < 0) {
                    std::shared_ptr<T> obj = nullptr;
                    {
                        std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                        while (task_que_.empty()) {
                            cv_.wait(guard);
                            if (join_ > 0) return;
                        }
                        obj = std::move(task_que_.front()); // 这里不直接接引用，拷贝一次，因此可以将 shared_ptr 弹出
                        task_que_.pop_front();
                    }
                    if (obj) obj->on_task();
                }
            }

        public:

            task_thread_pool() {
            }

            task_thread_pool(const int cnt) {
                if (allocate(cnt) < 0) {
                    throw ( std::string("failed to allocate task thread pool"));
                }
            }

            virtual ~task_thread_pool() {
                join();
                // 资源清理
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                task_que_.clear();
            }

            void join() {
                {
                    std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                    join_ = 1;
                    cv_.notify_all();
                }

                {
                    std::lock_guard < decltype(mem_lock_) > guard(mem_lock_);
                    auto iter = ths_.begin();
                    while (iter != ths_.end()) {
                        if (*iter) {
                            if ((*iter)->joinable()) {
                                (*iter)->join();
                            }
                            delete ( *iter);
                        }
                        iter = ths_.erase(iter);
                    }
                }
            }

            int allocate(const int cnt) {
                {
                    std::lock_guard < decltype(mem_lock_) > guard(mem_lock_);
                    if (ths_.size() > 0) {
                        return -1;
                    }
                }

                int alocnts = ((cnt > 0 && cnt < 32) ? (cnt) : (os::getnprocs()));

                for (int i = 0; i < alocnts; i++) {
                    std::thread *pth = new std::thread(std::bind(&task_thread_pool::th_handler, this));
                    ths_.push_back(pth);
                }
                return 0;
            }

            void post(const std::shared_ptr<T> &task) {
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                task_que_.push_back(task);
                cv_.notify_one();
            }
        };

        // 使用线程池, 带任务优先级的任务模型

        template<class T> class priority_task_thread_pool {
            std::priority_queue <priority_task<T> *, std::vector<priority_task<T> *>, pointer_compatible_compare <priority_task<T>>> priority_task_que_;
            std::vector<std::thread *> ths_;
            std::mutex task_locker_;
            std::atomic<int> join_{-1};
            std::condition_variable cv_;
            std::recursive_mutex mem_lock_;

            void pool_handler() {
                while (join_ < 0) {
                    priority_task<T> * ptr = nullptr;
                    {
                        std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                        while (priority_task_que_.empty()) {
                            cv_.wait(guard);
                            if (join_ > 0) return;
                        }
                        ptr = priority_task_que_.top();
                        priority_task_que_.pop();
                    }

                    if (ptr) {
                        ptr->reference().on_task();
                        delete ptr;
                    }
                }
            }

        public:

            priority_task_thread_pool() : task_thread_pool<T>() {
            }

            // 这里需要注意一下, 如果直接使用父类带CNT参数的构造， 则因为this还没有构造完成， 所以线程创建的虚函数pool_handler不会走到子类
            // 在构造函数体内进行 allocate 调用， 可以让虚函数生效

            priority_task_thread_pool(const int thcnt)// : task_thread_pool<T>( thcnt )
            {
                task_thread_pool<T>::allocate(thcnt);
            }

            ~priority_task_thread_pool() {
                join();

                // 清理未决请求
                std::lock_guard < decltype(task_locker_) > guard(task_locker_);
                while (!priority_task_que_.empty()) {
                    priority_task<T> *ptr = priority_task_que_.top();
                    priority_task_que_.pop();
                    delete ptr;
                }
            }

            int post(const std::shared_ptr<T> &tsk, const int priority = 0) {
                std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                try {
                    priority_task<T> *ptask = new priority_task<T>(priority, tsk);
                    priority_task_que_.push(ptask);
                } catch (...) {
                    return -1;
                }
                cv_.notify_one();
                return 0;
            }

            void join() {
                {
                    std::unique_lock < decltype(task_locker_) > guard(task_locker_);
                    join_ = 1;
                    cv_.notify_all();
                }

                {
                    std::lock_guard < decltype(mem_lock_) > guard(mem_lock_);
                    auto iter = ths_.begin();
                    while (iter != ths_.end()) {
                        if (*iter) {
                            if ((*iter)->joinable()) {
                                (*iter)->join();
                            }
                            delete ( *iter);
                        }
                        iter = ths_.erase(iter);
                    }
                }
            }
        };

    } // namespace toolkit
} // namespace base

#endif