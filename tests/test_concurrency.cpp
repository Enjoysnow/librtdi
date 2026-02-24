#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <set>

#include <atomic>
#include <thread>
#include <vector>

namespace {

struct ICounter {
    virtual ~ICounter() = default;
    virtual int id() const = 0;
};

static std::atomic<int> g_counter_id{0};

struct Counter : ICounter {
    int id_;
    Counter() : id_(++g_counter_id) {}
    int id() const override { return id_; }
};

} // namespace

TEST_CASE("concurrent singleton resolution yields same instance", "[concurrency]") {
    g_counter_id = 0;

    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    auto r = reg.build({.validate_on_build = false});

    constexpr std::size_t N = 16;
    std::vector<std::thread> threads;
    std::vector<ICounter*> results(N, nullptr);

    for (std::size_t i = 0; i < N; ++i) {
        threads.emplace_back([&, i] {
            results[i] = &r->get<ICounter>();
        });
    }
    for (auto& t : threads) t.join();

    // All threads must get the same instance
    for (std::size_t i = 1; i < N; ++i) {
        REQUIRE(results[i] == results[0]);
    }
    // Only one instance was created
    REQUIRE(results[0]->id() == 1);
}

TEST_CASE("concurrent transient creation yields different instances", "[concurrency]") {
    librtdi::registry reg;
    reg.add_transient<ICounter, Counter>();
    auto r = reg.build({.validate_on_build = false});

    constexpr std::size_t N = 16;
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<ICounter>> results(N);

    for (std::size_t i = 0; i < N; ++i) {
        threads.emplace_back([&, i] {
            results[i] = r->create<ICounter>();
        });
    }
    for (auto& t : threads) t.join();

    // All instances must be distinct
    std::set<ICounter*> ptrs;
    for (auto& p : results) {
        REQUIRE(p != nullptr);
        ptrs.insert(p.get());
    }
    REQUIRE(ptrs.size() == N);
}
