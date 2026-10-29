#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <concepts>
#include <type_traits>
#include <algorithm>
#include <boost/lockfree/queue.hpp>
#include "config.h"

class ThreadPool
{
  public:
    explicit ThreadPool (size_t threads_count)
    {
        workers_.reserve(threads_count);
        for (size_t i = 0; i < threads_count; ++i) {
            workers_.emplace_back([this] {
                std::stop_token stoken = stop_source_.get_token();
                std::mutex mutex;
                while (!stoken.stop_requested()) {
                    std::unique_lock lk{mutex};
                    task_cv_.wait(lk, stoken, [this] {
                        return tasks_.consume_one([] (auto task) {
                            (*task)();
                            delete task;
                        });
                    });
                }
                tasks_.consume_all([] (auto task) {
                    (*task)();
                    delete task;
                });
            });
        }
    }

    // Деструктор: сигнализирует об остановке и будит все потоки
    ~ThreadPool ()
    {
        stop_source_.request_stop();
        task_cv_.notify_all();
    }

    template <typename F, typename... Args>
        requires std::invocable<F, Args...>
    auto enqueue (F&& f, Args&&... args)
    {
        using return_type = std::invoke_result_t<F, Args...>;

        // returns an invalid future if stop requested
        if (stop_source_.stop_requested()) return std::future<return_type>{};

        std::packaged_task<return_type()> task{
          std::bind_front(std::forward<F>(f), std::forward<Args>(args)...)};
        std::future<return_type> res = task.get_future();

        auto P_task   = std::make_shared<decltype(task)>(std::move(task));
        auto P_lambda = ::new auto([P_task] mutable { (*P_task)(); });
        std::function<void()> func{[P_lambda] {
            (*P_lambda)();
            delete P_lambda;
        }};
        auto P_func = std::make_unique<decltype(func)>(std::move(func));

        using namespace std::chrono_literals;
        while (!tasks_.bounded_push(P_func.get())) {
            if (stop_source_.stop_requested()) {
                P_func.reset();
                return std::future<return_type>{};
            }
            std::this_thread::sleep_for(1us);
        }

        P_func.release();
        task_cv_.notify_one();
        return res;
    }

  private:
    using QueueType =
      boost::lockfree::queue<std::function<void()>*, boost::lockfree::capacity<128>>;

    QueueType tasks_;
    std::stop_source stop_source_;
    std::vector<std::jthread> workers_;
    std::condition_variable_any task_cv_;
};
