#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <librtdi.hpp>
#include <memory>

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

} // namespace

TEST_CASE("get singleton", "[resolution]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    auto& svc = r->get<IService>();
    REQUIRE(svc.value() == 1);
}

TEST_CASE("try_get returns pointer on success", "[resolution]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    auto* svc = r->try_get<IService>();
    REQUIRE(svc != nullptr);
    REQUIRE(svc->value() == 1);
}

TEST_CASE("try_get returns nullptr on not registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    auto* svc = r->try_get<IService>();
    REQUIRE(svc == nullptr);
}

TEST_CASE("get throws not_found when not registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(r->get<IService>(), librtdi::not_found);
}

TEST_CASE("create transient", "[resolution]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    auto svc = r->create<IService>();
    REQUIRE(svc != nullptr);
    REQUIRE(svc->value() == 1);
}

TEST_CASE("try_create returns nullptr when not registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    auto svc = r->try_create<IService>();
    REQUIRE(svc == nullptr);
}

TEST_CASE("create throws not_found when not registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(r->create<IService>(), librtdi::not_found);
}

TEST_CASE("get_all singleton collection", "[resolution]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceB>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);
    // Verify both values are present
    bool has1 = false, has2 = false;
    for (auto* p : all) {
        if (p->value() == 1) has1 = true;
        if (p->value() == 2) has2 = true;
    }
    REQUIRE(has1);
    REQUIRE(has2);
}

TEST_CASE("create_all transient collection", "[resolution]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IService, ServiceB>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IService>();
    REQUIRE(all.size() == 2);
}

TEST_CASE("get_all returns empty when no collection registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.empty());
}

TEST_CASE("create_all returns empty when no collection registered", "[resolution]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IService>();
    REQUIRE(all.empty());
}

// ---------------------------------------------------------------
// resolution_error wraps factory exceptions
// ---------------------------------------------------------------

TEST_CASE("resolution_error wraps factory exception", "[resolution]") {
    struct IFailing {
        virtual ~IFailing() = default;
    };
    struct FailingImpl : IFailing {
        FailingImpl() { throw std::runtime_error("factory boom"); }
    };

    librtdi::registry reg;
    reg.add_singleton<IFailing, FailingImpl>();
    auto r = reg.build({.validate_on_build = false,
                        .eager_singletons = false});

    try {
        r->get<IFailing>();
        FAIL("Expected resolution_error");
    } catch (const librtdi::resolution_error& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("factory boom"));
    }
}

TEST_CASE("resolution_error passes through di_error", "[resolution]") {
    struct IDep {
        virtual ~IDep() = default;
    };
    struct ISvc {
        virtual ~ISvc() = default;
    };
    struct Svc : ISvc {
        explicit Svc(IDep& /*dep*/) {}
    };

    librtdi::registry reg;
    // ISvc depends on IDep which is not registered
    reg.add_singleton<ISvc, Svc>(librtdi::deps<IDep>);
    auto r = reg.build({.validate_on_build = false,
                        .eager_singletons = false});

    // When resolving ISvc, resolving IDep will throw not_found;
    // resolver should NOT wrap di_error in resolution_error
    REQUIRE_THROWS_AS(r->get<ISvc>(), librtdi::not_found);
}

TEST_CASE("transient resolution_error wraps factory exception", "[resolution]") {
    struct IFailing {
        virtual ~IFailing() = default;
    };
    struct FailingImpl : IFailing {
        FailingImpl() { throw std::runtime_error("transient boom"); }
    };

    librtdi::registry reg;
    reg.add_transient<IFailing, FailingImpl>();
    auto r = reg.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(r->create<IFailing>(), librtdi::resolution_error);
}

// ---------------------------------------------------------------
// Slot hint diagnostics: not_found message includes usage hint
// ---------------------------------------------------------------

TEST_CASE("not_found hint: get<T> when only transient registered", "[resolution][diagnostics]") {
    librtdi::registry reg;
    reg.add_transient<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>();
        FAIL("expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("transient"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("create<T>()"));
    }
}

TEST_CASE("not_found hint: create<T> when only singleton registered", "[resolution][diagnostics]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    try {
        r->create<IService>();
        FAIL("expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("singleton"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("get<T>()"));
    }
}

TEST_CASE("not_found hint: get<T> when only collection registered", "[resolution][diagnostics]") {
    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>();
        FAIL("expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("collection"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("get_all<T>()"));
    }
}
