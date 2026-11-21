#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>

#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lifecycle_test
{
    // Only used in the dedicated lifecycle executables. Their parent process never starts
    // workers, so fork does not inherit runtime threads or their locked mutexes.
    template <typename Probe> void requireCleanExit(Probe probe)
    {
        const pid_t child = ::fork();
        REQUIRE(child >= 0);
        if (child == 0)
        {
            // Remove inherited Catch2 fatal-signal handlers: only the parent reports assertions.
            ::signal(SIGABRT, SIG_DFL);
            ::signal(SIGSEGV, SIG_DFL);
            ::signal(SIGBUS, SIG_DFL);
            ::signal(SIGALRM, SIG_DFL);
            const rlimit no_core{0, 0};
            ::setrlimit(RLIMIT_CORE, &no_core);
            ::alarm(10); // Final hang guard, never synchronization.
            try
            {
                const int result = probe();
                ::_exit(result);
            }
            catch (const std::exception &error)
            {
                std::fprintf(stderr, "lifecycle probe exception: %s\n", error.what());
                ::_exit(2);
            }
            catch (...)
            {
                std::fprintf(stderr, "lifecycle probe: unknown exception\n");
                ::_exit(2);
            }
        }

        int status = 0;
        pid_t waited;
        do
        {
            waited = ::waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        REQUIRE(waited == child);
        INFO("raw child wait status = " << status);
        if (WIFSIGNALED(status))
        {
            INFO("child signal = " << WTERMSIG(status) << " (" << ::strsignal(WTERMSIG(status))
                                   << ")");
            if (WTERMSIG(status) == SIGALRM)
            {
                INFO("10 second hang guard expired");
                REQUIRE(WIFEXITED(status));
            }
            REQUIRE(WIFEXITED(status));
        }
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == EXIT_SUCCESS);
    }

    struct Gate
    {
        void arriveAndWait()
        {
            std::unique_lock<std::mutex> lock(mutex);
            arrived = true;
            cv.notify_all();
            cv.wait(lock, [this] { return released; });
        }

        bool waitForArrival()
        {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::seconds(3), [this] { return arrived; });
        }

        void release()
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
            cv.notify_all();
        }

        std::mutex mutex;
        std::condition_variable cv;
        bool arrived{false};
        bool released{false};
    };

    // Declare after the worker owner so unwinding opens the gate before join/destruction.
    struct ReleaseGateOnExit
    {
        Gate &gate;
        ~ReleaseGateOnExit()
        {
            gate.release();
        }
    };
} // namespace lifecycle_test
