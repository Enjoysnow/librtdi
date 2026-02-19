#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces for validation
// ---------------------------------------------------------------

struct IValA {
    virtual ~IValA() = default;
};
struct IValB {
    virtual ~IValB() = default;
};
struct IValC {
    virtual ~IValC() = default;
};

struct ValAImpl : IValA {
    std::shared_ptr<IValB> b_;
    explicit ValAImpl(std::shared_ptr<IValB> b) : b_(std::move(b)) {}
};
struct ValBImpl : IValB {
    std::shared_ptr<IValC> c_;
    explicit ValBImpl(std::shared_ptr<IValC> c) : c_(std::move(c)) {}
};
struct ValCImpl : IValC {};

// Cyclic
struct ValCycA : IValA {
    std::shared_ptr<IValB> b_;
    explicit ValCycA(std::shared_ptr<IValB> b) : b_(std::move(b)) {}
};
struct ValCycB : IValB {
    std::shared_ptr<IValA> a_;
    explicit ValCycB(std::shared_ptr<IValA> a) : a_(std::move(a)) {}
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Validation: cyclic dependency throws", "[validation]") {
    registry registry;
    registry.add_singleton<IValA, ValCycA>(deps<IValB>);
    registry.add_singleton<IValB, ValCycB>(deps<IValA>);

    REQUIRE_THROWS_AS(
        registry.build(),
        cyclic_dependency);
}

TEST_CASE("Validation: missing dependency throws", "[validation]") {
    registry registry;
    registry.add_singleton<IValA, ValAImpl>(deps<IValB>);
    // IValB not registered

    REQUIRE_THROWS_AS(
        registry.build(),
        not_found);
}

TEST_CASE("Validation: singleton depending on scoped throws", "[validation]") {
    registry registry;
    registry.add_scoped<IValC, ValCImpl>();
    registry.add_singleton<IValB, ValBImpl>(deps<IValC>);

    REQUIRE_THROWS_AS(
        registry.build(),
        lifetime_mismatch);
}

TEST_CASE("Validation: singleton depending on transient throws", "[validation]") {
    registry registry;
    registry.add_transient<IValC, ValCImpl>();
    registry.add_singleton<IValB, ValBImpl>(deps<IValC>);

    REQUIRE_THROWS_AS(
        registry.build(),
        lifetime_mismatch);
}

TEST_CASE("Validation: scoped depending on singleton is OK", "[validation]") {
    registry registry;
    registry.add_singleton<IValC, ValCImpl>();
    registry.add_scoped<IValB, ValBImpl>(deps<IValC>);

    REQUIRE_NOTHROW(registry.build());
}

TEST_CASE("Validation: transient depending on anything is OK", "[validation]") {
    registry registry;
    registry.add_scoped<IValC, ValCImpl>();
    registry.add_transient<IValB, ValBImpl>(deps<IValC>);

    REQUIRE_NOTHROW(registry.build());
}

TEST_CASE("Validation: valid chain passes", "[validation]") {
    registry registry;
    registry.add_singleton<IValC, ValCImpl>();
    registry.add_singleton<IValB, ValBImpl>(deps<IValC>);
    registry.add_singleton<IValA, ValAImpl>(deps<IValB>);

    REQUIRE_NOTHROW(registry.build());
}

TEST_CASE("Validation: disabled validation allows missing deps", "[validation]") {
    registry registry;
    registry.add_singleton<IValA, ValAImpl>(deps<IValB>);

    build_options opts{.validate_on_build = false};
    REQUIRE_NOTHROW(registry.build(opts));
}

TEST_CASE("Validation: disabled scope validation allows singleton->scoped", "[validation]") {
    registry registry;
    registry.add_scoped<IValC, ValCImpl>();
    registry.add_singleton<IValB, ValBImpl>(deps<IValC>);

    build_options opts{.validate_on_build = true, .validate_scopes = false};
    REQUIRE_NOTHROW(registry.build(opts));
}

TEST_CASE("Validation: ambiguous dependency throws ambiguous_component", "[validation]") {
    // IValC registered twice; IValB depends on IValC via deps<>
    // build should detect that IValC has >1 non-keyed registration
    registry registry;
    registry.add_singleton<IValC, ValCImpl>();
    registry.add_singleton<IValC, ValCImpl>();    // duplicate
    registry.add_singleton<IValB, ValBImpl>(deps<IValC>);

    REQUIRE_THROWS_AS(registry.build(), ambiguous_component);
}

TEST_CASE("Validation: forward target with multiple registrations passes", "[validation]") {
    // forward expands to N descriptors. With 2 FwdImpl registrations,
    // IFwd gets 2. No one depends on IFwd, so no ambiguity error for
    // consumers. The expanded descriptors' deps on FwdImpl are exempt.
    struct IFwd {
        virtual ~IFwd() = default;
    };
    struct FwdImpl : IFwd {};

    registry registry;
    registry.add_singleton<FwdImpl, FwdImpl>();
    registry.add_singleton<FwdImpl, FwdImpl>();  // two registrations of target
    registry.forward<IFwd, FwdImpl>();

    REQUIRE_NOTHROW(registry.build());
}
