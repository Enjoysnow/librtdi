#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>

namespace {

struct IA {
    virtual ~IA() = default;
};
struct A : IA {};

struct IB {
    virtual ~IB() = default;
};
struct B : IB {
    explicit B(IA& /*a*/) {}
};

struct IC {
    virtual ~IC() = default;
};
struct C : IC {
    explicit C(IB& /*b*/) {}
};

// Cyclic: X depends on Y depends on X
struct IX {
    virtual ~IX() = default;
};
struct IY {
    virtual ~IY() = default;
};
struct X : IX {
    explicit X(IY& /*y*/) {}
};
struct Y : IY {
    explicit Y(IX& /*x*/) {}
};

} // namespace

TEST_CASE("missing dependency detected", "[validation]") {
    librtdi::registry reg;
    // B depends on IA but IA is not registered
    reg.add_singleton<IB, B>(librtdi::deps<IA>);
    REQUIRE_THROWS_AS(reg.build(), librtdi::not_found);
}

TEST_CASE("all deps satisfied passes validation", "[validation]") {
    librtdi::registry reg;
    reg.add_singleton<IA, A>();
    reg.add_singleton<IB, B>(librtdi::deps<IA>);
    REQUIRE_NOTHROW(reg.build());
}

TEST_CASE("cycle detected", "[validation]") {
    librtdi::registry reg;
    reg.add_singleton<IX, X>(librtdi::deps<IY>);
    reg.add_singleton<IY, Y>(librtdi::deps<IX>);
    REQUIRE_THROWS_AS(reg.build(), librtdi::cyclic_dependency);
}

TEST_CASE("lifetime mismatch: singleton depends on transient", "[validation]") {
    struct ISingleton {
        virtual ~ISingleton() = default;
    };
    struct ITransient {
        virtual ~ITransient() = default;
    };
    struct TransientImpl : ITransient {};
    struct SingletonImpl : ISingleton {
        explicit SingletonImpl(std::unique_ptr<ITransient> /*t*/) {}
    };

    librtdi::registry reg;
    reg.add_transient<ITransient, TransientImpl>();
    reg.add_singleton<ISingleton, SingletonImpl>(
        librtdi::deps<librtdi::transient<ITransient>>);
    REQUIRE_THROWS_AS(
        reg.build({.validate_lifetimes = true}),
        librtdi::lifetime_mismatch);
}

TEST_CASE("lifetime validation disabled passes", "[validation]") {
    struct ISingleton {
        virtual ~ISingleton() = default;
    };
    struct ITransient {
        virtual ~ITransient() = default;
    };
    struct TransientImpl : ITransient {};
    struct SingletonImpl : ISingleton {
        explicit SingletonImpl(std::unique_ptr<ITransient> /*t*/) {}
    };

    librtdi::registry reg;
    reg.add_transient<ITransient, TransientImpl>();
    reg.add_singleton<ISingleton, SingletonImpl>(
        librtdi::deps<librtdi::transient<ITransient>>);
    REQUIRE_NOTHROW(reg.build({.validate_lifetimes = false}));
}

TEST_CASE("validation disabled entirely", "[validation]") {
    librtdi::registry reg;
    // B depends on IA which is not registered
    reg.add_singleton<IB, B>(librtdi::deps<IA>);
    REQUIRE_NOTHROW(reg.build({.validate_on_build = false,
                               .eager_singletons = false}));
}

TEST_CASE("transient depending on singleton is ok", "[validation]") {
    librtdi::registry reg;
    reg.add_singleton<IA, A>();
    reg.add_transient<IB, B>(librtdi::deps<IA>);
    REQUIRE_NOTHROW(reg.build());
}

TEST_CASE("chain A→B→C validates", "[validation]") {
    librtdi::registry reg;
    reg.add_singleton<IA, A>();
    reg.add_singleton<IB, B>(librtdi::deps<IA>);
    reg.add_singleton<IC, C>(librtdi::deps<IB>);
    REQUIRE_NOTHROW(reg.build());
}

// ---------------------------------------------------------------
// detect_cycles = false allows cyclic deps to pass
// ---------------------------------------------------------------

TEST_CASE("detect_cycles disabled passes cyclic deps", "[validation]") {
    librtdi::registry reg;
    reg.add_singleton<IX, X>(librtdi::deps<IY>);
    reg.add_singleton<IY, Y>(librtdi::deps<IX>);
    REQUIRE_NOTHROW(reg.build({.validate_lifetimes = true,
                                .detect_cycles = false,
                                .eager_singletons = false}));
}

// ---------------------------------------------------------------
// Singleton with transient collection dep is allowed (not captive)
// ---------------------------------------------------------------

TEST_CASE("singleton with transient collection dep is allowed", "[validation]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
    };
    struct Plugin : IPlugin {};

    struct IHost {
        virtual ~IHost() = default;
    };
    struct Host : IHost {
        explicit Host(std::vector<std::unique_ptr<IPlugin>> /*ps*/) {}
    };

    librtdi::registry reg;
    reg.add_collection<IPlugin, Plugin>(librtdi::lifetime_kind::transient);
    reg.add_singleton<IHost, Host>(
        librtdi::deps<librtdi::collection<librtdi::transient<IPlugin>>>);
    // is_transient && is_collection → allowed per captive dependency rules
    REQUIRE_NOTHROW(reg.build());
}

// ---------------------------------------------------------------
// Missing collection dependency detected
// ---------------------------------------------------------------

TEST_CASE("missing collection dependency detected", "[validation]") {
    struct IPlugin {
        virtual ~IPlugin() = default;
    };
    struct IHost {
        virtual ~IHost() = default;
    };
    struct Host : IHost {
        explicit Host(std::vector<IPlugin*> /*ps*/) {}
    };

    librtdi::registry reg;
    // IPlugin collection is NOT registered
    reg.add_singleton<IHost, Host>(
        librtdi::deps<librtdi::collection<IPlugin>>);
    REQUIRE_THROWS_AS(reg.build(), librtdi::not_found);
}

// ---------------------------------------------------------------
// Missing transient dependency detected
// ---------------------------------------------------------------

TEST_CASE("missing transient dependency detected", "[validation]") {
    struct IDep {
        virtual ~IDep() = default;
    };
    struct ISvc {
        virtual ~ISvc() = default;
    };
    struct Svc : ISvc {
        explicit Svc(std::unique_ptr<IDep> /*d*/) {}
    };

    librtdi::registry reg;
    // IDep transient is NOT registered
    reg.add_transient<ISvc, Svc>(librtdi::deps<librtdi::transient<IDep>>);
    REQUIRE_THROWS_AS(reg.build(), librtdi::not_found);
}
