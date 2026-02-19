#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

using namespace librtdi;

// ---------------------------------------------------------------
// Test interfaces & implementations
// ---------------------------------------------------------------

struct IAlpha {
    virtual ~IAlpha() = default;
    virtual std::string Name() const = 0;
};

struct AlphaImpl : IAlpha {
    std::string Name() const override { return "Alpha"; }
};

struct IBeta {
    virtual ~IBeta() = default;
    virtual std::string Name() const = 0;
};

struct BetaImpl : IBeta {
    std::shared_ptr<IAlpha> alpha_;
    explicit BetaImpl(std::shared_ptr<IAlpha> a) : alpha_(std::move(a)) {}
    std::string Name() const override { return "Beta->" + alpha_->Name(); }
};

struct IGamma {
    virtual ~IGamma() = default;
    virtual std::string Name() const = 0;
};

struct GammaImpl : IGamma {
    std::shared_ptr<IBeta> beta_;
    explicit GammaImpl(std::shared_ptr<IBeta> b) : beta_(std::move(b)) {}
    std::string Name() const override { return "Gamma->" + beta_->Name(); }
};

// ---------------------------------------------------------------
// Tests
// ---------------------------------------------------------------

TEST_CASE("Resolution: resolve component with no dependencies", "[resolution]") {
    registry registry;
    registry.add_singleton<IAlpha, AlphaImpl>();
    auto resolver = registry.build();

    auto svc = resolver->resolve<IAlpha>();
    REQUIRE(svc != nullptr);
    REQUIRE(svc->Name() == "Alpha");
}

TEST_CASE("Resolution: try_resolve returns nullptr for unregistered", "[resolution]") {
    registry registry;
    auto resolver = registry.build({.validate_on_build = false});

    auto svc = resolver->try_resolve<IAlpha>();
    REQUIRE(svc == nullptr);
}

TEST_CASE("Resolution: resolve throws for unregistered", "[resolution]") {
    registry registry;
    auto resolver = registry.build({.validate_on_build = false});

    REQUIRE_THROWS_AS(
        resolver->resolve<IAlpha>(),
        not_found);
}

TEST_CASE("Resolution: 2-level dependency chain", "[resolution]") {
    registry registry;
    registry.add_singleton<IAlpha, AlphaImpl>();
    registry.add_singleton<IBeta, BetaImpl>(deps<IAlpha>);
    auto resolver = registry.build();

    auto svc = resolver->resolve<IBeta>();
    REQUIRE(svc->Name() == "Beta->Alpha");
}

TEST_CASE("Resolution: 3-level dependency chain", "[resolution]") {
    registry registry;
    registry.add_singleton<IAlpha, AlphaImpl>();
    registry.add_singleton<IBeta, BetaImpl>(deps<IAlpha>);
    registry.add_singleton<IGamma, GammaImpl>(deps<IBeta>);
    auto resolver = registry.build();

    auto svc = resolver->resolve<IGamma>();
    REQUIRE(svc->Name() == "Gamma->Beta->Alpha");
}

TEST_CASE("Resolution: diamond dependency singleton constructed once", "[resolution]") {
    static int alpha_count = 0;
    alpha_count = 0;

    struct AlphaCounting : IAlpha {
        AlphaCounting() { ++alpha_count; }
        std::string Name() const override { return "AlphaC"; }
    };

    struct ILeft {
        virtual ~ILeft() = default;
    };
    struct LeftImpl : ILeft {
        std::shared_ptr<IAlpha> a_;
        explicit LeftImpl(std::shared_ptr<IAlpha> a) : a_(std::move(a)) {}
    };
    struct IRight {
        virtual ~IRight() = default;
    };
    struct RightImpl : IRight {
        std::shared_ptr<IAlpha> a_;
        explicit RightImpl(std::shared_ptr<IAlpha> a) : a_(std::move(a)) {}
    };

    registry registry;
    registry.add_singleton<IAlpha, AlphaCounting>();
    registry.add_singleton<ILeft, LeftImpl>(deps<IAlpha>);
    registry.add_singleton<IRight, RightImpl>(deps<IAlpha>);
    auto resolver = registry.build();

    auto left = resolver->resolve<ILeft>();
    auto right = resolver->resolve<IRight>();
    REQUIRE(alpha_count == 1); // Singleton — only one instance created
}
