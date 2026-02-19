#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <string>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IDatabase {
    virtual ~IDatabase() = default;
    virtual std::string Name() const = 0;
};

struct SqliteDb : IDatabase {
    std::string Name() const override { return "sqlite"; }
};

struct PostgresDb : IDatabase {
    std::string Name() const override { return "postgres"; }
};

struct MySqlDb : IDatabase {
    std::string Name() const override { return "mysql"; }
};

struct ICache {
    virtual ~ICache() = default;
    virtual std::string Type() const = 0;
};

struct RedisCache : ICache {
    std::string Type() const override { return "redis"; }
};

struct MemoryCache : ICache {
    std::string Type() const override { return "memory"; }
};

struct IService {
    virtual ~IService() = default;
    virtual std::string DbName() const = 0;
};

struct ServiceUsingDb : IService {
    std::shared_ptr<IDatabase> db_;
    explicit ServiceUsingDb(std::shared_ptr<IDatabase> db) : db_(std::move(db)) {}
    std::string DbName() const override { return db_->Name(); }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Keyed: resolve by key returns correct implementation", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("local");
    registry.add_singleton<IDatabase, PostgresDb>("remote");
    auto resolver = registry.build();

    auto local = resolver->resolve<IDatabase>("local");
    auto remote = resolver->resolve<IDatabase>("remote");
    REQUIRE(local->Name() == "sqlite");
    REQUIRE(remote->Name() == "postgres");
}

TEST_CASE("Keyed: different keys are independent instances", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("a");
    registry.add_singleton<IDatabase, SqliteDb>("b");
    auto resolver = registry.build();

    auto a = resolver->resolve<IDatabase>("a");
    auto b = resolver->resolve<IDatabase>("b");
    // Different singletons for different keys
    REQUIRE(a.get() != b.get());
}

TEST_CASE("Keyed: try_resolve returns nullptr for missing key", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("existing");
    auto resolver = registry.build();

    auto svc = resolver->try_resolve<IDatabase>("nonexistent");
    REQUIRE(svc == nullptr);
}

TEST_CASE("Keyed: resolve throws for missing key", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("existing");
    auto resolver = registry.build();

    REQUIRE_THROWS_AS(
        resolver->resolve<IDatabase>("nonexistent"),
        not_found);
}

TEST_CASE("Keyed: resolve_all with key returns all for that key", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("primary");
    registry.add_singleton<IDatabase, PostgresDb>("primary");
    registry.add_singleton<IDatabase, MySqlDb>("secondary");
    auto resolver = registry.build();

    auto primary = resolver->resolve_all<IDatabase>("primary");
    REQUIRE(primary.size() == 2);
    REQUIRE(primary[0]->Name() == "sqlite");
    REQUIRE(primary[1]->Name() == "postgres");

    auto secondary = resolver->resolve_all<IDatabase>("secondary");
    REQUIRE(secondary.size() == 1);
    REQUIRE(secondary[0]->Name() == "mysql");
}

TEST_CASE("Keyed: keyed and non-keyed coexist independently", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>();           // non-keyed
    registry.add_singleton<IDatabase, PostgresDb>("backup"); // keyed
    auto resolver = registry.build();

    auto def = resolver->resolve<IDatabase>();
    auto backup = resolver->resolve<IDatabase>("backup");
    REQUIRE(def->Name() == "sqlite");
    REQUIRE(backup->Name() == "postgres");

    // Non-keyed resolve_all should NOT include keyed
    auto all_default = resolver->resolve_all<IDatabase>();
    REQUIRE(all_default.size() == 1);
    REQUIRE(all_default[0]->Name() == "sqlite");
}

TEST_CASE("Keyed: singleton lifetime per key", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("k1");
    auto resolver = registry.build();

    auto a = resolver->resolve<IDatabase>("k1");
    auto b = resolver->resolve<IDatabase>("k1");
    REQUIRE(a.get() == b.get());
}

TEST_CASE("Keyed: scoped lifetime per key per scope", "[keyed]") {
    registry registry;
    registry.add_scoped<IDatabase, SqliteDb>("k1");
    auto resolver = registry.build();

    auto scope1 = resolver->create_scope();
    auto scope2 = resolver->create_scope();

    auto a = scope1->get_resolver().resolve<IDatabase>("k1");
    auto b = scope1->get_resolver().resolve<IDatabase>("k1");
    auto c = scope2->get_resolver().resolve<IDatabase>("k1");

    REQUIRE(a.get() == b.get());  // same scope → same instance
    REQUIRE(a.get() != c.get());  // different scope → different instance
}

TEST_CASE("Keyed: transient lifetime per key", "[keyed]") {
    registry registry;
    registry.add_transient<IDatabase, SqliteDb>("k1");
    auto resolver = registry.build();

    auto scope = resolver->create_scope();
    auto a = scope->get_resolver().resolve<IDatabase>("k1");
    auto b = scope->get_resolver().resolve<IDatabase>("k1");
    REQUIRE(a.get() != b.get());
}

TEST_CASE("Keyed: deps<> with keyed registration", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, PostgresDb>("primary");
    // ServiceUsingDb depends on IDatabase (non-keyed)
    registry.add_singleton<IDatabase, SqliteDb>();
    registry.add_singleton<IService, ServiceUsingDb>(deps<IDatabase>);
    auto resolver = registry.build();

    // Non-keyed IDatabase used for deps<>
    auto svc = resolver->resolve<IService>();
    REQUIRE(svc->DbName() == "sqlite");
}

TEST_CASE("Keyed: validation detects missing keyed dependency", "[keyed][validation]") {
    registry registry;
    // register_component IDatabase under key "special" but
    // ServiceUsingDb depends on non-keyed IDatabase
    registry.add_singleton<IDatabase, SqliteDb>("special");
    registry.add_singleton<IService, ServiceUsingDb>(deps<IDatabase>);

    // Non-keyed IDatabase is missing → validation should catch it
    REQUIRE_THROWS_AS(
        registry.build(),
        not_found);
}

TEST_CASE("Keyed: resolve_any returns last for keyed", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("db");
    registry.add_singleton<IDatabase, PostgresDb>("db");
    auto resolver = registry.build();

    auto db = resolver->resolve_any<IDatabase>("db");
    REQUIRE(db->Name() == "postgres");  // last-wins
}

TEST_CASE("Keyed: try_resolve_any returns nullptr for missing key", "[keyed]") {
    registry registry;
    registry.add_singleton<IDatabase, SqliteDb>("existing");
    auto resolver = registry.build();

    auto db = resolver->try_resolve_any<IDatabase>("nonexistent");
    REQUIRE(db == nullptr);
}
