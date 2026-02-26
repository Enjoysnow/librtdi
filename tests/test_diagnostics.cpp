#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <librtdi.hpp>
#include <string>

namespace {

struct IService {
    virtual ~IService() = default;
};
struct ServiceImpl : IService {};

} // namespace

TEST_CASE("not_found includes type name", "[diagnostics]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>();
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IService"));
    }
}

TEST_CASE("duplicate_registration includes type name", "[diagnostics]") {
    librtdi::registry reg;
    reg.add_singleton<IService, ServiceImpl>();

    try {
        reg.add_singleton<IService, ServiceImpl>();
        FAIL("Expected duplicate_registration");
    } catch (const librtdi::duplicate_registration& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IService"));
    }
}

TEST_CASE("cyclic_dependency includes cycle path", "[diagnostics]") {
    struct IA { virtual ~IA() = default; };
    struct IB { virtual ~IB() = default; };
    struct A : IA { explicit A(IB&) {} };
    struct B : IB { explicit B(IA&) {} };

    librtdi::registry reg;
    reg.add_singleton<IA, A>(librtdi::deps<IB>);
    reg.add_singleton<IB, B>(librtdi::deps<IA>);

    try {
        reg.build();
        FAIL("Expected cyclic_dependency");
    } catch (const librtdi::cyclic_dependency& e) {
        REQUIRE(e.cycle().size() >= 2);
    }
}

TEST_CASE("cyclic_dependency message format is correct", "[diagnostics]") {
    struct IX { virtual ~IX() = default; };
    struct IY { virtual ~IY() = default; };
    struct XImpl : IX { explicit XImpl(IY&) {} };
    struct YImpl : IY { explicit YImpl(IX&) {} };

    librtdi::registry reg;
    reg.add_singleton<IX, XImpl>(librtdi::deps<IY>);
    reg.add_singleton<IY, YImpl>(librtdi::deps<IX>);

    try {
        reg.build();
        FAIL("Expected cyclic_dependency");
    } catch (const librtdi::cyclic_dependency& e) {
        std::string msg = e.what();
        // Cycle path should be [A -> B -> A], NOT [A -> B -> A -> A]
        // Count occurrences of " -> " — should be exactly 2 for a 2-node cycle
        std::size_t arrow_count = 0;
        std::size_t pos = 0;
        while ((pos = msg.find(" -> ", pos)) != std::string::npos) {
            ++arrow_count;
            pos += 4;
        }
        // e.g. "Cyclic dependency detected: IX -> IY -> IX [at ...]" → 2 arrows
        REQUIRE(arrow_count == 2);
    }
}

TEST_CASE("di_error carries source_location", "[diagnostics]") {
    try {
        throw librtdi::di_error("test error");
    } catch (const librtdi::di_error& e) {
        // source_location should point to this file
        std::string loc = e.location().file_name();
        REQUIRE(!loc.empty());
    }
}

// ---------------------------------------------------------------
// lifetime_mismatch diagnostic includes type names
// ---------------------------------------------------------------

TEST_CASE("lifetime_mismatch includes consumer and dependency", "[diagnostics]") {
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

    try {
        reg.build({.validate_lifetimes = true});
        FAIL("Expected lifetime_mismatch");
    } catch (const librtdi::lifetime_mismatch& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("singleton"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("transient"));
    }
}

// ---------------------------------------------------------------
// resolution_error diagnostic includes type name and inner message
// ---------------------------------------------------------------

TEST_CASE("resolution_error includes type and inner message", "[diagnostics]") {
    struct IFailing {
        virtual ~IFailing() = default;
    };
    struct FailingImpl : IFailing {
        FailingImpl() { throw std::runtime_error("intentional failure"); }
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
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("intentional failure"));
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("IFailing"));
    }
}

// ---------------------------------------------------------------
// not_found with key includes key in message
// ---------------------------------------------------------------

TEST_CASE("not_found with key includes key string", "[diagnostics]") {
    librtdi::registry reg;
    auto r = reg.build({.validate_on_build = false});

    try {
        r->get<IService>("my_key");
        FAIL("Expected not_found");
    } catch (const librtdi::not_found& e) {
        std::string msg = e.what();
        REQUIRE_THAT(msg, Catch::Matchers::ContainsSubstring("my_key"));
    }
}
