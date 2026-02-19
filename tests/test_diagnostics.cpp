#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;
using Catch::Matchers::ContainsSubstring;

// ---------------------------------------------------------------
// Test interfaces
// ---------------------------------------------------------------

struct IDiagA {
    virtual ~IDiagA() = default;
};
struct IDiagB {
    virtual ~IDiagB() = default;
};
struct IDiagC {
    virtual ~IDiagC() = default;
};

struct DiagAImpl : IDiagA {
    std::shared_ptr<IDiagB> b_;
    explicit DiagAImpl(std::shared_ptr<IDiagB> b) : b_(std::move(b)) {}
};
struct DiagBImpl : IDiagB {
    std::shared_ptr<IDiagA> a_;
    explicit DiagBImpl(std::shared_ptr<IDiagA> a) : a_(std::move(a)) {}
};
struct DiagCImpl : IDiagC {};

struct DiagAWithC : IDiagA {
    std::shared_ptr<IDiagC> c_;
    explicit DiagAWithC(std::shared_ptr<IDiagC> c) : c_(std::move(c)) {}
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Diagnostics: cyclic exception contains cycle path", "[diagnostics]") {
    registry registry;
    registry.add_singleton<IDiagA, DiagAImpl>(deps<IDiagB>);
    registry.add_singleton<IDiagB, DiagBImpl>(deps<IDiagA>);

    try {
        registry.build();
        FAIL("Expected cyclic_dependency");
    } catch (const cyclic_dependency& ex) {
        std::string what = ex.what();
        // Should contain both types in the cycle
        CHECK_THAT(what, ContainsSubstring("IDiagA"));
        CHECK_THAT(what, ContainsSubstring("IDiagB"));
        CHECK_THAT(what, ContainsSubstring("->"));
    }
}

TEST_CASE("Diagnostics: missing dep exception contains type name", "[diagnostics]") {
    registry registry;
    registry.add_singleton<IDiagA, DiagAImpl>(deps<IDiagB>);

    try {
        registry.build();
        FAIL("Expected not_found");
    } catch (const not_found& ex) {
        std::string what = ex.what();
        CHECK_THAT(what, ContainsSubstring("IDiagB"));
    }
}

TEST_CASE("Diagnostics: lifetime mismatch exception contains both sides", "[diagnostics]") {
    registry registry;
    registry.add_scoped<IDiagC, DiagCImpl>();
    registry.add_singleton<IDiagA, DiagAWithC>(deps<IDiagC>);

    try {
        registry.build();
        FAIL("Expected lifetime_mismatch");
    } catch (const lifetime_mismatch& ex) {
        std::string what = ex.what();
        CHECK_THAT(what, ContainsSubstring("IDiagA"));
        CHECK_THAT(what, ContainsSubstring("IDiagC"));
        CHECK_THAT(what, ContainsSubstring("singleton"));
        CHECK_THAT(what, ContainsSubstring("scoped"));
    }
}

TEST_CASE("Diagnostics: exception contains source location info", "[diagnostics]") {
    registry registry;
    registry.add_singleton<IDiagA, DiagAImpl>(deps<IDiagB>);

    try {
        registry.build();
        FAIL("Expected exception");
    } catch (const di_error& ex) {
        std::string what = ex.what();
        // Should contain file info from source_location
        CHECK_THAT(what, ContainsSubstring("[at "));
    }
}

TEST_CASE("Diagnostics: ambiguous_component contains type name", "[diagnostics]") {
    registry registry;
    registry.add_singleton<IDiagC, DiagCImpl>();
    registry.add_singleton<IDiagC, DiagCImpl>();
    registry.add_singleton<IDiagA, DiagAWithC>(deps<IDiagC>);

    try {
        registry.build();
        FAIL("Expected ambiguous_component");
    } catch (const ambiguous_component& ex) {
        std::string what = ex.what();
        CHECK_THAT(what, ContainsSubstring("IDiagC"));
        CHECK_THAT(what, ContainsSubstring("Ambiguous"));
        CHECK_THAT(what, ContainsSubstring("multiple registrations"));
    }
}
