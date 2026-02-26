#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>

namespace {

struct IService {
    virtual ~IService() = default;
    virtual std::string name() const = 0;
};

struct RealService : IService {
    std::string name() const override { return "real"; }
};

struct LoggingDecorator : IService {
    librtdi::decorated_ptr<IService> inner_;
    explicit LoggingDecorator(librtdi::decorated_ptr<IService> inner)
        : inner_(std::move(inner)) {}
    std::string name() const override { return "logged(" + inner_->name() + ")"; }
};

struct CachingDecorator : IService {
    librtdi::decorated_ptr<IService> inner_;
    explicit CachingDecorator(librtdi::decorated_ptr<IService> inner)
        : inner_(std::move(inner)) {}
    std::string name() const override { return "cached(" + inner_->name() + ")"; }
};

} // namespace

TEST_CASE("basic decorator wraps singleton", "[decorator]") {
    librtdi::registry reg;
    reg.add_singleton<IService, RealService>();
    reg.decorate<IService, LoggingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& svc = r->get<IService>();
    REQUIRE(svc.name() == "logged(real)");
}

TEST_CASE("basic decorator wraps transient", "[decorator]") {
    librtdi::registry reg;
    reg.add_transient<IService, RealService>();
    reg.decorate<IService, LoggingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto svc = r->create<IService>();
    REQUIRE(svc->name() == "logged(real)");
}

TEST_CASE("multiple decorators stack", "[decorator]") {
    librtdi::registry reg;
    reg.add_singleton<IService, RealService>();
    reg.decorate<IService, LoggingDecorator>();
    reg.decorate<IService, CachingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& svc = r->get<IService>();
    REQUIRE(svc.name() == "cached(logged(real))");
}

TEST_CASE("multiple decorators stack on transient", "[decorator]") {
    librtdi::registry reg;
    reg.add_transient<IService, RealService>();
    reg.decorate<IService, LoggingDecorator>();
    reg.decorate<IService, CachingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto svc = r->create<IService>();
    REQUIRE(svc->name() == "cached(logged(real))");

    // Each create() returns a fresh decorated chain
    auto svc2 = r->create<IService>();
    REQUIRE(svc2->name() == "cached(logged(real))");
    REQUIRE(svc.get() != svc2.get());
}

TEST_CASE("decorator with extra deps", "[decorator]") {
    struct IConfig {
        virtual ~IConfig() = default;
        virtual std::string prefix() const = 0;
    };
    struct Config : IConfig {
        std::string prefix() const override { return "PREFIX"; }
    };

    struct PrefixDecorator : IService {
        librtdi::decorated_ptr<IService> inner_;
        IConfig& config_;
        PrefixDecorator(librtdi::decorated_ptr<IService> inner, IConfig& config)
            : inner_(std::move(inner)), config_(config) {}
        std::string name() const override {
            return config_.prefix() + ":" + inner_->name();
        }
    };

    librtdi::registry reg;
    reg.add_singleton<IConfig, Config>();
    reg.add_singleton<IService, RealService>();
    reg.decorate<IService, PrefixDecorator>(librtdi::deps<IConfig>);
    auto r = reg.build();

    auto& svc = r->get<IService>();
    REQUIRE(svc.name() == "PREFIX:real");
}

TEST_CASE("decorator targets specific impl", "[decorator]") {
    struct ServiceX : IService {
        std::string name() const override { return "X"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceX>(librtdi::lifetime_kind::singleton);
    // Only decorate RealService, not ServiceX
    reg.decorate<IService, LoggingDecorator>(std::type_index(typeid(RealService)));
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_logged = false;
    bool found_plain_x = false;
    for (auto* p : all) {
        if (p->name() == "logged(real)") found_logged = true;
        if (p->name() == "X") found_plain_x = true;
    }
    REQUIRE(found_logged);
    REQUIRE(found_plain_x);
}

// ---------------------------------------------------------------
// Decorator on transient creates new decorated instance each time
// ---------------------------------------------------------------

TEST_CASE("decorator on transient creates new each time", "[decorator]") {
    librtdi::registry reg;
    reg.add_transient<IService, RealService>();
    reg.decorate<IService, LoggingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto a = r->create<IService>();
    auto b = r->create<IService>();
    REQUIRE(a.get() != b.get());
    REQUIRE(a->name() == "logged(real)");
    REQUIRE(b->name() == "logged(real)");
}

// ---------------------------------------------------------------
// Decorator applied to collection items
// ---------------------------------------------------------------

TEST_CASE("decorator applies to collection items", "[decorator]") {
    struct ServiceY : IService {
        std::string name() const override { return "Y"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceY>(librtdi::lifetime_kind::singleton);
    // Decorate ALL IService registrations
    reg.decorate<IService, LoggingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_logged_real = false;
    bool found_logged_y = false;
    for (auto* p : all) {
        if (p->name() == "logged(real)") found_logged_real = true;
        if (p->name() == "logged(Y)") found_logged_y = true;
    }
    REQUIRE(found_logged_real);
    REQUIRE(found_logged_y);
}

TEST_CASE("decorator applies to transient collection items", "[decorator]") {
    struct ServiceY : IService {
        std::string name() const override { return "Y"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::transient);
    reg.add_collection<IService, ServiceY>(librtdi::lifetime_kind::transient);
    reg.decorate<IService, LoggingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_logged_real = false;
    bool found_logged_y = false;
    for (auto& p : all) {
        if (p->name() == "logged(real)") found_logged_real = true;
        if (p->name() == "logged(Y)") found_logged_y = true;
    }
    REQUIRE(found_logged_real);
    REQUIRE(found_logged_y);

    // Each create_all returns fresh instances
    auto all2 = r->create_all<IService>();
    REQUIRE(all2.size() == 2);
    REQUIRE(all[0].get() != all2[0].get());
}

TEST_CASE("multiple decorators on singleton collection", "[decorator]") {
    struct ServiceY : IService {
        std::string name() const override { return "Y"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceY>(librtdi::lifetime_kind::singleton);
    reg.decorate<IService, LoggingDecorator>();
    reg.decorate<IService, CachingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_cached_logged_real = false;
    bool found_cached_logged_y = false;
    for (auto* p : all) {
        if (p->name() == "cached(logged(real))") found_cached_logged_real = true;
        if (p->name() == "cached(logged(Y))") found_cached_logged_y = true;
    }
    REQUIRE(found_cached_logged_real);
    REQUIRE(found_cached_logged_y);
}

TEST_CASE("multiple decorators on transient collection", "[decorator]") {
    struct ServiceY : IService {
        std::string name() const override { return "Y"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::transient);
    reg.add_collection<IService, ServiceY>(librtdi::lifetime_kind::transient);
    reg.decorate<IService, LoggingDecorator>();
    reg.decorate<IService, CachingDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_cached_logged_real = false;
    bool found_cached_logged_y = false;
    for (auto& p : all) {
        if (p->name() == "cached(logged(real))") found_cached_logged_real = true;
        if (p->name() == "cached(logged(Y))") found_cached_logged_y = true;
    }
    REQUIRE(found_cached_logged_real);
    REQUIRE(found_cached_logged_y);
}

TEST_CASE("decorator with extra deps on collection", "[decorator]") {
    struct IConfig {
        virtual ~IConfig() = default;
        virtual std::string prefix() const = 0;
    };
    struct Config : IConfig {
        std::string prefix() const override { return "PFX"; }
    };

    struct PrefixDecorator : IService {
        librtdi::decorated_ptr<IService> inner_;
        IConfig& config_;
        PrefixDecorator(librtdi::decorated_ptr<IService> inner, IConfig& config)
            : inner_(std::move(inner)), config_(config) {}
        std::string name() const override {
            return config_.prefix() + ":" + inner_->name();
        }
    };

    struct ServiceY : IService {
        std::string name() const override { return "Y"; }
    };

    librtdi::registry reg;
    reg.add_singleton<IConfig, Config>();
    reg.add_collection<IService, RealService>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceY>(librtdi::lifetime_kind::singleton);
    reg.decorate<IService, PrefixDecorator>(librtdi::deps<IConfig>);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_pfx_real = false;
    bool found_pfx_y = false;
    for (auto* p : all) {
        if (p->name() == "PFX:real") found_pfx_real = true;
        if (p->name() == "PFX:Y") found_pfx_y = true;
    }
    REQUIRE(found_pfx_real);
    REQUIRE(found_pfx_y);
}

// ---------------------------------------------------------------
// Decorator with transient extra dep
// ---------------------------------------------------------------

TEST_CASE("decorator with transient extra dep", "[decorator]") {
    struct ITag {
        virtual ~ITag() = default;
        virtual std::string tag() const = 0;
    };
    struct Tag : ITag {
        std::string tag() const override { return "TAG"; }
    };

    struct TagDecorator : IService {
        librtdi::decorated_ptr<IService> inner_;
        std::unique_ptr<ITag> tag_;
        TagDecorator(librtdi::decorated_ptr<IService> inner, std::unique_ptr<ITag> tag)
            : inner_(std::move(inner)), tag_(std::move(tag)) {}
        std::string name() const override {
            return tag_->tag() + ":" + inner_->name();
        }
    };

    librtdi::registry reg;
    reg.add_transient<ITag, Tag>();
    reg.add_singleton<IService, RealService>();
    reg.decorate<IService, TagDecorator>(
        librtdi::deps<librtdi::transient<ITag>>);
    auto r = reg.build({.validate_lifetimes = false});

    REQUIRE(r->get<IService>().name() == "TAG:real");
}

// ---------------------------------------------------------------
// decorate_target<I, D, TTarget>() — type-safe targeted decoration
// ---------------------------------------------------------------

TEST_CASE("decorate_target applies only to specified impl", "[decorator]") {
    struct ServiceA : IService {
        std::string name() const override { return "A"; }
    };
    struct ServiceB : IService {
        std::string name() const override { return "B"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::singleton);
    reg.add_collection<IService, ServiceB>(librtdi::lifetime_kind::singleton);
    // Only decorate ServiceA via type-safe overload (no type_index)
    reg.decorate_target<IService, LoggingDecorator, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_logged_a = false;
    bool found_plain_b = false;
    for (auto* p : all) {
        if (p->name() == "logged(A)") found_logged_a = true;
        if (p->name() == "B") found_plain_b = true;
    }
    REQUIRE(found_logged_a);
    REQUIRE(found_plain_b);
}

TEST_CASE("decorate_target with extra deps", "[decorator]") {
    struct IConfig {
        virtual ~IConfig() = default;
        virtual std::string tag() const = 0;
    };
    struct Config : IConfig {
        std::string tag() const override { return "CFG"; }
    };

    struct TargetedDec : IService {
        librtdi::decorated_ptr<IService> inner_;
        IConfig& cfg_;
        TargetedDec(librtdi::decorated_ptr<IService> inner, IConfig& cfg)
            : inner_(std::move(inner)), cfg_(cfg) {}
        std::string name() const override {
            return cfg_.tag() + ":" + inner_->name();
        }
    };

    librtdi::registry reg;
    reg.add_singleton<IConfig, Config>();
    reg.add_singleton<IService, RealService>();
    reg.decorate_target<IService, TargetedDec, RealService>(librtdi::deps<IConfig>);
    auto r = reg.build();

    REQUIRE(r->get<IService>().name() == "CFG:real");
}

TEST_CASE("decorate_target on transient collection", "[decorator]") {
    struct ServiceA : IService {
        std::string name() const override { return "A"; }
    };
    struct ServiceB : IService {
        std::string name() const override { return "B"; }
    };

    librtdi::registry reg;
    reg.add_collection<IService, ServiceA>(librtdi::lifetime_kind::transient);
    reg.add_collection<IService, ServiceB>(librtdi::lifetime_kind::transient);
    // Only decorate ServiceA
    reg.decorate_target<IService, LoggingDecorator, ServiceA>();
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<IService>();
    REQUIRE(all.size() == 2);

    bool found_logged_a = false;
    bool found_plain_b = false;
    for (auto& p : all) {
        if (p->name() == "logged(A)") found_logged_a = true;
        if (p->name() == "B") found_plain_b = true;
    }
    REQUIRE(found_logged_a);
    REQUIRE(found_plain_b);
}
