#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

static int g_factory_calls = 0;

struct ICounter {
    virtual ~ICounter() = default;
    virtual int count() const = 0;
};

struct Counter : ICounter {
    Counter() { ++g_factory_calls; }
    int count() const override { return g_factory_calls; }
};

struct IService {
    virtual ~IService() = default;
    virtual int value() const = 0;
};

struct Service : IService {
    Service() { ++g_factory_calls; }
    int value() const override { return 1; }
};

struct IBroken {
    virtual ~IBroken() = default;
};

struct Broken : IBroken {
    Broken() { throw std::runtime_error("factory failed"); }
};

} // namespace

// ---------------------------------------------------------------
// Eager singletons: all singleton factories called during build()
// ---------------------------------------------------------------

TEST_CASE("eager_singletons instantiates all singletons during build",
          "[eager]") {
    g_factory_calls = 0;
    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    reg.add_singleton<IService, Service>();

    // Default eager_singletons = true
    auto r = reg.build({.validate_on_build = false});

    // Both factories should have been called during build()
    REQUIRE(g_factory_calls == 2);

    // Subsequent get() returns the same pre-created instances (no extra calls)
    auto& c = r->get<ICounter>();
    auto& s = r->get<IService>();
    REQUIRE(g_factory_calls == 2);
    REQUIRE(c.count() == 2);
    REQUIRE(s.value() == 1);
}

// ---------------------------------------------------------------
// Eager does not affect transients
// ---------------------------------------------------------------

TEST_CASE("eager_singletons does not affect transients", "[eager]") {
    g_factory_calls = 0;
    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    reg.add_transient<IService, Service>();

    auto r = reg.build({.validate_on_build = false});

    // Only singleton factory called during build, transient untouched
    REQUIRE(g_factory_calls == 1);

    // Transient created on demand
    auto svc = r->create<IService>();
    REQUIRE(g_factory_calls == 2);
}

// ---------------------------------------------------------------
// Factory exception surfaces at build time
// ---------------------------------------------------------------

TEST_CASE("eager_singletons propagates factory exception from build",
          "[eager]") {
    librtdi::registry reg;
    reg.add_singleton<IBroken, Broken>();

    REQUIRE_THROWS_AS(
        reg.build({.validate_on_build = false}),
        std::runtime_error);
}

// ---------------------------------------------------------------
// Explicit eager_singletons = false keeps lazy behavior
// ---------------------------------------------------------------

TEST_CASE("eager_singletons = false keeps lazy behavior", "[eager]") {
    g_factory_calls = 0;
    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();

    auto r = reg.build({.validate_on_build = false,
                        .eager_singletons = false});

    // No factory calls during build
    REQUIRE(g_factory_calls == 0);

    // Created on first get()
    auto& c = r->get<ICounter>();
    REQUIRE(g_factory_calls == 1);
    static_cast<void>(c);
}

// ---------------------------------------------------------------
// Eager with forward singleton — original created, forward reuses
// ---------------------------------------------------------------

TEST_CASE("eager_singletons with forward singleton", "[eager][forward]") {
    g_factory_calls = 0;

    struct IBase {
        virtual ~IBase() = default;
        virtual int id() const = 0;
    };
    struct IDerived : IBase {};
    struct Impl : IDerived {
        Impl() { ++g_factory_calls; }
        int id() const override { return 42; }
    };

    librtdi::registry reg;
    reg.add_singleton<IDerived, Impl>();
    reg.forward<IBase, IDerived>();

    auto r = reg.build({.validate_on_build = false});

    // Original singleton eagerly created; forward singleton delegates to same
    REQUIRE(g_factory_calls == 1);
    REQUIRE(r->get<IBase>().id() == 42);
    REQUIRE(g_factory_calls == 1);  // no extra creation
}

// ---------------------------------------------------------------
// Eager with collection singletons
// ---------------------------------------------------------------

TEST_CASE("eager_singletons with collection", "[eager]") {
    g_factory_calls = 0;

    struct IPlugin {
        virtual ~IPlugin() = default;
        virtual std::string name() const = 0;
    };
    struct PluginA : IPlugin {
        PluginA() { ++g_factory_calls; }
        std::string name() const override { return "A"; }
    };
    struct PluginB : IPlugin {
        PluginB() { ++g_factory_calls; }
        std::string name() const override { return "B"; }
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, PluginA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IPlugin, PluginB>(librtdi::lifetime_kind::singleton);

    auto r = reg.build({.validate_on_build = false});

    // Both collection singletons eagerly created
    REQUIRE(g_factory_calls == 2);

    auto all = r->get_all<IPlugin>();
    REQUIRE(all.size() == 2);
    REQUIRE(g_factory_calls == 2);  // no extra creation
}

// ---------------------------------------------------------------
// Eager with decorated singleton
// ---------------------------------------------------------------

TEST_CASE("eager_singletons with decorated singleton", "[eager][decorator]") {
    g_factory_calls = 0;

    struct LoggingCounter : ICounter {
        librtdi::decorated_ptr<ICounter> inner_;
        explicit LoggingCounter(librtdi::decorated_ptr<ICounter> inner)
            : inner_(std::move(inner)) { ++g_factory_calls; }
        int count() const override { return inner_->count(); }
    };

    librtdi::registry reg;
    reg.add_singleton<ICounter, Counter>();
    reg.decorate<ICounter, LoggingCounter>();

    auto r = reg.build({.validate_on_build = false});

    // Both Counter + LoggingCounter factories called during build
    REQUIRE(g_factory_calls == 2);

    auto& c = r->get<ICounter>();
    REQUIRE(g_factory_calls == 2);
    static_cast<void>(c);
}
