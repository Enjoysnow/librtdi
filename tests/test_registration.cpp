#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

namespace {

struct IService {
    virtual ~IService() = default;
    virtual int value() const = 0;
};

struct ServiceA : IService {
    int value() const override { return 1; }
};

struct ServiceB : IService {
    int value() const override { return 2; }
};

struct ILogger {
    virtual ~ILogger() = default;
    virtual std::string name() const = 0;
};

struct ConsoleLogger : ILogger {
    std::string name() const override { return "console"; }
};

} // namespace

TEST_CASE("register singleton zero-dep", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE(r != nullptr);
}

TEST_CASE("register transient zero-dep", "[registration]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE(r != nullptr);
}

TEST_CASE("duplicate singleton throws", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    REQUIRE_THROWS_AS(
        (reg.add_singleton<IService, ServiceB>()),
        librtdi::duplicate_registration);
}

TEST_CASE("duplicate transient throws", "[registration]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>();
    REQUIRE_THROWS_AS(
        (reg.add_transient<IService, ServiceB>()),
        librtdi::duplicate_registration);
}

TEST_CASE("singleton + transient same type ok", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    reg.add_transient<IService, ServiceB>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE(r != nullptr);
}

TEST_CASE("collection allows multiple registrations", "[registration]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceB>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});
    REQUIRE(r != nullptr);
}

TEST_CASE("register after build throws", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE_THROWS_AS(
        (reg.add_singleton<ILogger, ConsoleLogger>()),
        librtdi::di_error);
}

TEST_CASE("build twice throws", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE_THROWS_AS(
        reg.build({.validate_on_build = false}),
        librtdi::di_error);
}

TEST_CASE("fluent chaining", "[registration]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>()
       .add_transient<ILogger, ConsoleLogger>();
    auto r = reg.build({.validate_on_build = false});
    REQUIRE(r != nullptr);
}
