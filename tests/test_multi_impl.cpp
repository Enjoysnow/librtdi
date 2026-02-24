#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <set>

namespace {

struct IPlugin {
    virtual ~IPlugin() = default;
    virtual std::string name() const = 0;
};

struct PluginA : IPlugin {
    std::string name() const override { return "A"; }
};

struct PluginB : IPlugin {
    std::string name() const override { return "B"; }
};

struct PluginC : IPlugin {
    std::string name() const override { return "C"; }
};

} // namespace

TEST_CASE("singleton collection multiple items", "[multi_impl]") {
    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginC>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IPlugin>();
    REQUIRE(all.size() == 3);

    std::set<std::string> names;
    for (auto* p : all) names.insert(p->name());
    REQUIRE(names.count("A"));
    REQUIRE(names.count("B"));
    REQUIRE(names.count("C"));
}

TEST_CASE("transient collection multiple items", "[multi_impl]") {
    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    auto all1 = r->create_all<IPlugin>();
    auto all2 = r->create_all<IPlugin>();
    REQUIRE(all1.size() == 2);
    REQUIRE(all2.size() == 2);
    // Each call creates fresh instances
    REQUIRE(all1[0].get() != all2[0].get());
}

TEST_CASE("collection dep injection singleton", "[multi_impl]") {
    struct IAggregator {
        virtual ~IAggregator() = default;
        virtual std::size_t count() const = 0;
    };

    struct Aggregator : IAggregator {
        std::vector<IPlugin*> plugins;
        explicit Aggregator(std::vector<IPlugin*> ps)
            : plugins(std::move(ps)) {}
        std::size_t count() const override { return plugins.size(); }
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::singleton);
    reg.add_singleton<IAggregator, Aggregator>(
        librtdi::deps<librtdi::collection<IPlugin>>);
    auto r = reg.build();

    auto& agg = r->get<IAggregator>();
    REQUIRE(agg.count() == 2);
}

TEST_CASE("collection dep injection transient", "[multi_impl]") {
    struct IAggregator {
        virtual ~IAggregator() = default;
        virtual std::size_t count() const = 0;
    };

    struct Aggregator : IAggregator {
        std::vector<std::unique_ptr<IPlugin>> plugins;
        explicit Aggregator(std::vector<std::unique_ptr<IPlugin>> ps)
            : plugins(std::move(ps)) {}
        std::size_t count() const override { return plugins.size(); }
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::transient);
    reg.add_singleton<IAggregator, Aggregator>(
        librtdi::deps<librtdi::collection<librtdi::transient<IPlugin>>>);
    auto r = reg.build();

    auto& agg = r->get<IAggregator>();
    REQUIRE(agg.count() == 2);
}

// ---------------------------------------------------------------
// Mixed singleton + transient collections on same interface
// ---------------------------------------------------------------

TEST_CASE("mixed singleton and transient collections coexist", "[multi_impl]") {
    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    // Singleton collection: only PluginA
    auto singletons = r->get_all<IPlugin>();
    REQUIRE(singletons.size() == 1);
    REQUIRE(singletons[0]->name() == "A");

    // Transient collection: only PluginB
    auto transients = r->create_all<IPlugin>();
    REQUIRE(transients.size() == 1);
    REQUIRE(transients[0]->name() == "B");
}

// ---------------------------------------------------------------
// Collection with deps
// ---------------------------------------------------------------

TEST_CASE("collection with deps", "[multi_impl]") {
    struct ILogger {
        virtual ~ILogger() = default;
        virtual std::string name() const = 0;
    };
    struct Logger : ILogger {
        std::string name() const override { return "logger"; }
    };

    struct PluginWithDep : IPlugin {
        ILogger& logger_;
        explicit PluginWithDep(ILogger& logger) : logger_(logger) {}
        std::string name() const override { return "dep:" + logger_.name(); }
    };

    librtdi::registry reg;
    reg.add_singleton<ILogger, Logger>();
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginWithDep>(
        librtdi::lifetime_kind::singleton,
        librtdi::deps<ILogger>);
    auto r = reg.build();

    auto all = r->get_all<IPlugin>();
    REQUIRE(all.size() == 2);

    bool found_dep = false;
    for (auto* p : all) {
        if (p->name() == "dep:logger") found_dep = true;
    }
    REQUIRE(found_dep);
}
