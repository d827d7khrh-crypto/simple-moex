#pragma once

#include <cstdint>
#include <boost/atomic/thread_pause.hpp>
#include <thread>
#include <chrono>

namespace misc
{

template <typename Fn>
constexpr inline void spin_wait (Fn&& f, size_t microsec)
{
    size_t spins = 0;
    while (!std::forward<Fn>(f)()) {
        if (++spins < 100) {
            boost::atomics::thread_pause();
        } else if (spins < 110) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(microsec));
        }
    }
}

template <typename Fn, typename ST>
constexpr inline void spin_wait (Fn&& f, ST&& st, size_t microsec)
{
    size_t spins = 0;
    while (!std::forward<Fn>(f)()) {
        if (std::forward<ST>(st).stop_requested()) return;
        if (++spins < 100) {
            boost::atomics::thread_pause();
        } else if (spins < 110) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(microsec));
        }
    }
}

} // namespace misc
