#pragma once

//
// PLEASE READ: Do you really need a singleton?
//
// Singletons make it hard to determine the lifetime of an object, which can
// lead to buggy code and spurious crashes.
//
// Instead of adding another singleton into the mix, try to identify either:
//   a) An existing singleton that can manage your object's lifetime
//   b) Locations where you can deterministically create the object and pass
//      into other objects
//
// If you absolutely need a singleton, please keep them as trivial as possible
// and ideally a leaf dependency. Singletons get problematic when they attempt
// to do too much in their destructor or have circular dependencies.
//

#include <mutex>
#include <atomic>

namespace nsp {
    namespace toolkit {

        template <class T>
        class singleton {
        public:

            static T* instance() {
                // 使用带原子锁的DoubleCheckNull 进行最大层度的线程安全校验
                T* tmp = instance_.load(std::memory_order::memory_order_acquire);
                if (!tmp) {
                    std::lock_guard<std::recursive_mutex> guard(lock_);
                    tmp = instance_.load(std::memory_order::memory_order_relaxed);
                    if (!tmp) {
                        tmp = new T;
                        associated_ = 0;
                        instance_.store(tmp, std::memory_order::memory_order_release);
                    }
                }
                return tmp;
            }

            static void associate(T *ptr) {
                // 使用带原子锁的DoubleCheckNull 进行最大层度的线程安全校验
                T* tmp = instance_.load(std::memory_order::memory_order_acquire);
                if (!tmp) {
                    std::lock_guard<std::recursive_mutex> guard(lock_);
                    tmp = instance_.load(std::memory_order::memory_order_relaxed);
                    if (!tmp) {
                        tmp = ptr;
                        associated_ = 1;
                        instance_.store(tmp, std::memory_order::memory_order_release);
                    }
                }
            }

            static void release() {
                T* tmp = instance_.load(std::memory_order::memory_order_acquire);
                if (tmp && !associated_) {
                    std::lock_guard<std::recursive_mutex> guard(lock_);
                    tmp = instance_.load(std::memory_order::memory_order_relaxed);
                    if (tmp && !associated_) {
                        delete tmp;
                        instance_.store(nullptr, std::memory_order::memory_order_release);
                    }
                }
            }

        private:

            singleton() {
            }

            ~singleton() {
            }

            singleton(const singleton&) = delete;
            singleton(singleton&&) = delete;
            singleton& operator=(const singleton&) = delete;
            singleton& operator=(singleton&&) = delete;

        private:
            static std::atomic<T*> instance_;
            static std::recursive_mutex lock_;
            static int associated_;
        };

        template <class T>
        std::atomic<T*> singleton<T>::instance_ = {nullptr};

        template <class T>
        std::recursive_mutex singleton<T>::lock_;

        template <class T>
        int singleton<T>::associated_ = 0;

    } // toolkit
} // nsp