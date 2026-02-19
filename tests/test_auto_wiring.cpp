#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces for auto-wiring with deps<>
// ---------------------------------------------------------------

struct IEngine {
    virtual ~IEngine() = default;
    virtual std::string Type() const = 0;
};

struct ITransmission {
    virtual ~ITransmission() = default;
    virtual std::string Type() const = 0;
};

struct ICar {
    virtual ~ICar() = default;
    virtual std::string Describe() const = 0;
};

struct V8Engine : IEngine {
    std::string Type() const override { return "V8"; }
};

struct AutomaticTransmission : ITransmission {
    std::string Type() const override { return "Automatic"; }
};

struct Sedan : ICar {
    std::shared_ptr<IEngine> engine_;
    std::shared_ptr<ITransmission> transmission_;
    Sedan(std::shared_ptr<IEngine> e, std::shared_ptr<ITransmission> t)
        : engine_(std::move(e)), transmission_(std::move(t)) {}
    std::string Describe() const override {
        return engine_->Type() + "-" + transmission_->Type();
    }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("AutoWiring: deps<> tag correctly resolves 2 dependencies", "[autowiring]") {
    registry registry;
    registry.add_singleton<IEngine, V8Engine>();
    registry.add_singleton<ITransmission, AutomaticTransmission>();
    registry.add_singleton<ICar, Sedan>(deps<IEngine, ITransmission>);
    auto resolver = registry.build();

    auto car = resolver->resolve<ICar>();
    REQUIRE(car->Describe() == "V8-Automatic");
}

TEST_CASE("AutoWiring: deps<> extracts dependency list for validation", "[autowiring]") {
    registry registry;
    registry.add_singleton<IEngine, V8Engine>();
    // ITransmission NOT registered
    registry.add_singleton<ICar, Sedan>(deps<IEngine, ITransmission>);

    REQUIRE_THROWS_AS(
        registry.build(),
        not_found);
}

TEST_CASE("AutoWiring: zero-dependency auto-wiring", "[autowiring]") {
    registry registry;
    registry.add_singleton<IEngine, V8Engine>();
    auto resolver = registry.build();

    auto engine = resolver->resolve<IEngine>();
    REQUIRE(engine->Type() == "V8");
}

TEST_CASE("AutoWiring: ambiguous dependency caught at build", "[autowiring]") {
    // Sedan depends on IEngine via deps<>. Two IEngine registrations.
    // build should throw ambiguous_component at validation time.
    struct TurboEngine : IEngine {
        std::string Type() const override { return "Turbo"; }
    };

    registry registry;
    registry.add_singleton<IEngine, V8Engine>();
    registry.add_singleton<IEngine, TurboEngine>();
    registry.add_singleton<ITransmission, AutomaticTransmission>();
    registry.add_singleton<ICar, Sedan>(deps<IEngine, ITransmission>);

    REQUIRE_THROWS_AS(registry.build(), ambiguous_component);
}
