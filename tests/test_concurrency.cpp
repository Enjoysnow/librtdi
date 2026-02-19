#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IConcurrent {
    virtual ~IConcurrent() = default;
};

struct ConcurrentImpl : IConcurrent {
    static std::atomic<int> construct_count;
    ConcurrentImpl() {
        ++construct_count;
        // Sleep briefly to widen the race window
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
};
std::atomic<int> ConcurrentImpl::construct_count{0};

struct IScopedConcurrent {
    virtual ~IScopedConcurrent() = default;
};

struct ScopedConcurrentImpl : IScopedConcurrent {
    static std::atomic<int> construct_count;
    ScopedConcurrentImpl() {
        ++construct_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
};
std::atomic<int> ScopedConcurrentImpl::construct_count{0};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Concurrency: singleton resolved once under contention", "[concurrency]") {
    ConcurrentImpl::construct_count = 0;

    registry registry;
    registry.add_singleton<IConcurrent, ConcurrentImpl>();
    auto resolver = registry.build();

    constexpr std::size_t N = 32;
    std::vector<std::jthread> threads;
    std::vector<std::shared_ptr<IConcurrent>> results(N);

    for (std::size_t i = 0; i < N; ++i) {
        threads.emplace_back([&, i] {
            results[i] = resolver->resolve<IConcurrent>();
        });
    }
    threads.clear(); // join all

    REQUIRE(ConcurrentImpl::construct_count == 1);
    // All results should point to the same instance
    for (std::size_t i = 1; i < N; ++i) {
        REQUIRE(results[i].get() == results[0].get());
    }
}

TEST_CASE("Concurrency: concurrent scope creation and destruction", "[concurrency]") {
    struct IScopedConc {
        virtual ~IScopedConc() = default;
    };
    struct ScopedConcImpl : IScopedConc {};

    registry registry;
    registry.add_scoped<IScopedConc, ScopedConcImpl>();
    auto resolver = registry.build();

    constexpr int N = 32;
    std::vector<std::jthread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            auto scope = resolver->create_scope();
            auto svc = scope->get_resolver().resolve<IScopedConc>();
            REQUIRE(svc != nullptr);
            ++success_count;
        });
    }
    threads.clear();

    REQUIRE(success_count == N);
}

TEST_CASE("Concurrency: same scope resolves scoped once under contention", "[concurrency]") {
    ScopedConcurrentImpl::construct_count = 0;

    registry registry;
    registry.add_scoped<IScopedConcurrent, ScopedConcurrentImpl>();
    auto resolver = registry.build();

    auto scope = resolver->create_scope();
    auto& scoped_resolver = scope->get_resolver();

    constexpr std::size_t N = 32;
    std::vector<std::jthread> threads;
    std::vector<std::shared_ptr<IScopedConcurrent>> results(N);

    for (std::size_t i = 0; i < N; ++i) {
        threads.emplace_back([&, i] {
            results[i] = scoped_resolver.resolve<IScopedConcurrent>();
        });
    }
    threads.clear();

    REQUIRE(ScopedConcurrentImpl::construct_count == 1);
    for (std::size_t i = 1; i < N; ++i) {
        REQUIRE(results[i].get() == results[0].get());
    }
}
