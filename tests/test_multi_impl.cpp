#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <vector>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IPlugin {
    virtual ~IPlugin() = default;
    virtual std::string Name() const = 0;
};

struct PluginA : IPlugin {
    std::string Name() const override { return "A"; }
};

struct PluginB : IPlugin {
    std::string Name() const override { return "B"; }
};

struct PluginC : IPlugin {
    std::string Name() const override { return "C"; }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("MultiImpl: resolve_all returns all registered", "[multi]") {
    registry registry;
    registry.add_singleton<IPlugin, PluginA>();
    registry.add_singleton<IPlugin, PluginB>();
    registry.add_singleton<IPlugin, PluginC>();
    auto resolver = registry.build();

    auto plugins = resolver->resolve_all<IPlugin>();
    REQUIRE(plugins.size() == 3);
    REQUIRE(plugins[0]->Name() == "A");
    REQUIRE(plugins[1]->Name() == "B");
    REQUIRE(plugins[2]->Name() == "C");
}

TEST_CASE("MultiImpl: resolve throws for multiple registrations", "[multi]") {
    registry registry;
    registry.add_singleton<IPlugin, PluginA>();
    registry.add_singleton<IPlugin, PluginB>();
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(resolver->resolve<IPlugin>(), ambiguous_component);
}

TEST_CASE("MultiImpl: resolve_any returns last registered", "[multi]") {
    registry registry;
    registry.add_singleton<IPlugin, PluginA>();
    registry.add_singleton<IPlugin, PluginB>();
    auto resolver = registry.build();

    auto svc = resolver->resolve_any<IPlugin>();
    REQUIRE(svc->Name() == "B");
}

TEST_CASE("MultiImpl: each has independent lifetime", "[multi]") {
    registry registry;
    registry.add_singleton<IPlugin, PluginA>();
    registry.add_transient<IPlugin, PluginB>();
    auto resolver = registry.build();

    // resolve from a scope so both lifetimes work
    auto scope = resolver->create_scope();
    auto& r = scope->get_resolver();
    auto plugins1 = r.resolve_all<IPlugin>();
    auto plugins2 = r.resolve_all<IPlugin>();

    // First plugin (Singleton) should be same instance
    REQUIRE(plugins1[0].get() == plugins2[0].get());
    // Second plugin (Transient) should be different instances
    REQUIRE(plugins1[1].get() != plugins2[1].get());
}

TEST_CASE("MultiImpl: resolve_all for unregistered returns empty", "[multi]") {
    registry registry;
    registry.add_singleton<PluginA, PluginA>();
    auto resolver = registry.build();

    auto plugins = resolver->resolve_all<IPlugin>();
    REQUIRE(plugins.empty());
}
