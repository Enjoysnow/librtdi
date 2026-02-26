#include <catch2/catch_test_macros.hpp>
#include <librtdi.hpp>
#include <memory>
#include <string>

// ===============================================================
// Multiple Inheritance (MI) test fixtures
// ===============================================================

namespace {

// Track destructor calls for correctness verification
static int g_dtor_count = 0;

struct IAnimal {
    virtual ~IAnimal() = default;
    virtual std::string species() const = 0;
};

struct ISwimmable {
    virtual ~ISwimmable() = default;
    virtual int swim_speed() const = 0;
};

struct IFlyable {
    virtual ~IFlyable() = default;
    virtual int fly_speed() const = 0;
};

// MI: Duck implements both IAnimal and ISwimmable
struct Duck : IAnimal, ISwimmable {
    Duck() { ++g_dtor_count; }  // count constructions via dtor reset
    ~Duck() override { --g_dtor_count; }
    std::string species() const override { return "duck"; }
    int swim_speed() const override { return 5; }
};

// MI: FlyingFish implements IAnimal, ISwimmable, IFlyable (3 bases)
struct FlyingFish : IAnimal, ISwimmable, IFlyable {
    std::string species() const override { return "flying_fish"; }
    int swim_speed() const override { return 10; }
    int fly_speed() const override { return 3; }
};

// ===============================================================
// Virtual Inheritance (VI) test fixtures
// ===============================================================

struct IShape {
    virtual ~IShape() = default;
    virtual std::string shape_name() const = 0;
};

struct IDrawable : virtual IShape {
    virtual void draw() const = 0;
};

struct IResizable : virtual IShape {
    virtual void resize(int) const = 0;
};

// Diamond: DrawableResizableRect inherits IDrawable and IResizable,
// both virtually inherit IShape → single IShape subobject
struct DrawableResizableRect : IDrawable, IResizable {
    std::string shape_name() const override { return "rect"; }
    void draw() const override {}
    void resize(int) const override {}
};

// Simple virtual inheritance chain (non-diamond)
struct VBase {
    virtual ~VBase() = default;
    virtual int id() const = 0;
};

struct VMid : virtual VBase {
    int id() const override { return 99; }
};

struct VLeaf : VMid {
    int id() const override { return 42; }
};

} // namespace

// ===============================================================
// MI: Basic registration and resolution
// ===============================================================

TEST_CASE("MI: register and resolve via first base (IAnimal)", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_singleton<IAnimal, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto& animal = r->get<IAnimal>();
    REQUIRE(animal.species() == "duck");
}

TEST_CASE("MI: register and resolve via second base (ISwimmable)", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_singleton<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(swimmer.swim_speed() == 5);
}

TEST_CASE("MI: register same impl under two interfaces independently", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_singleton<IAnimal, Duck>();
    reg.add_singleton<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto& animal = r->get<IAnimal>();
    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(animal.species() == "duck");
    REQUIRE(swimmer.swim_speed() == 5);
    // These are different singleton instances (registered separately)
    REQUIRE(static_cast<const void*>(&animal) != static_cast<const void*>(&swimmer));
}

TEST_CASE("MI: transient via non-first base", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_transient<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto a = r->create<ISwimmable>();
    auto b = r->create<ISwimmable>();
    REQUIRE(a->swim_speed() == 5);
    REQUIRE(b->swim_speed() == 5);
    REQUIRE(a.get() != b.get());
}

TEST_CASE("MI: three bases, resolve each independently", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_singleton<IAnimal, FlyingFish>();
    reg.add_singleton<ISwimmable, FlyingFish>();
    reg.add_singleton<IFlyable, FlyingFish>();
    auto r = reg.build({.validate_on_build = false});

    REQUIRE(r->get<IAnimal>().species() == "flying_fish");
    REQUIRE(r->get<ISwimmable>().swim_speed() == 10);
    REQUIRE(r->get<IFlyable>().fly_speed() == 3);
}

// ===============================================================
// MI: Correct destruction
// ===============================================================

TEST_CASE("MI: destructor called exactly once for singleton via non-first base", "[inheritance][mi]") {
    g_dtor_count = 0;
    {
        librtdi::registry reg;
        reg.add_singleton<ISwimmable, Duck>();
        auto r = reg.build({.validate_on_build = false});
        auto& s = r->get<ISwimmable>();
        REQUIRE(s.swim_speed() == 5);
        REQUIRE(g_dtor_count == 1); // 1 construction
    }
    // After resolver is destroyed, dtor should have run → count back to 0
    REQUIRE(g_dtor_count == 0);
}

TEST_CASE("MI: transient destruction correctness via non-first base", "[inheritance][mi]") {
    g_dtor_count = 0;
    {
        auto ptr = []{
            librtdi::registry reg;
            reg.add_transient<ISwimmable, Duck>();
            auto r = reg.build({.validate_on_build = false});
            return r->create<ISwimmable>();
        }();
        REQUIRE(g_dtor_count == 1);
        REQUIRE(ptr->swim_speed() == 5);
    }
    REQUIRE(g_dtor_count == 0);
}

// ===============================================================
// MI: Collection
// ===============================================================

TEST_CASE("MI: collection via non-first base", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_collection<ISwimmable, Duck>(librtdi::lifetime_kind::singleton);
    reg.add_collection<ISwimmable, FlyingFish>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<ISwimmable>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->swim_speed() == 5);
    REQUIRE(all[1]->swim_speed() == 10);
}

TEST_CASE("MI: transient collection via non-first base", "[inheritance][mi]") {
    librtdi::registry reg;
    reg.add_collection<ISwimmable, Duck>(librtdi::lifetime_kind::transient);
    reg.add_collection<ISwimmable, FlyingFish>(librtdi::lifetime_kind::transient);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->create_all<ISwimmable>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->swim_speed() == 5);
    REQUIRE(all[1]->swim_speed() == 10);
}

// ===============================================================
// MI: Forward
// ===============================================================

TEST_CASE("MI: forward singleton from impl to non-first base", "[inheritance][mi][forward]") {
    // Register Duck as Duck, forward to ISwimmable (non-first base)
    librtdi::registry reg;
    reg.add_singleton<Duck, Duck>();
    reg.forward<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto& duck = r->get<Duck>();
    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(swimmer.swim_speed() == 5);
    // Should be the same underlying object
    REQUIRE(static_cast<ISwimmable*>(&duck) == &swimmer);
}

TEST_CASE("MI: forward transient from impl to non-first base", "[inheritance][mi][forward]") {
    librtdi::registry reg;
    reg.add_transient<Duck, Duck>();
    reg.forward<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto swimmer = r->create<ISwimmable>();
    REQUIRE(swimmer != nullptr);
    REQUIRE(swimmer->swim_speed() == 5);
}

TEST_CASE("MI: forward between two different interface bases", "[inheritance][mi][forward]") {
    // Register as IAnimal, forward to ISwimmable via Duck
    // This requires Duck registered under IAnimal, then forward ISwimmable from Duck
    // But forward<ISwimmable, IAnimal> would require IAnimal derives from ISwimmable - not true.
    // Instead: register as Duck, forward to both IAnimal and ISwimmable
    librtdi::registry reg;
    reg.add_singleton<Duck, Duck>();
    reg.forward<IAnimal, Duck>();
    reg.forward<ISwimmable, Duck>();
    auto r = reg.build({.validate_on_build = false});

    auto& animal = r->get<IAnimal>();
    auto& swimmer = r->get<ISwimmable>();
    auto& duck = r->get<Duck>();
    REQUIRE(animal.species() == "duck");
    REQUIRE(swimmer.swim_speed() == 5);
    // All point to the same Duck instance
    REQUIRE(static_cast<IAnimal*>(&duck) == &animal);
    REQUIRE(static_cast<ISwimmable*>(&duck) == &swimmer);
}

// ===============================================================
// MI: Decorator
// ===============================================================

TEST_CASE("MI: decorate via non-first base", "[inheritance][mi][decorator]") {
    struct SwimDecorator : ISwimmable {
        librtdi::decorated_ptr<ISwimmable> inner_;
        explicit SwimDecorator(librtdi::decorated_ptr<ISwimmable> inner) : inner_(std::move(inner)) {}
        int swim_speed() const override { return inner_->swim_speed() * 2; }
    };

    librtdi::registry reg;
    reg.add_singleton<ISwimmable, Duck>();
    reg.decorate<ISwimmable, SwimDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(swimmer.swim_speed() == 10); // 5 * 2
}

// ===============================================================
// MI: Forward + Decorator combined
// ===============================================================

TEST_CASE("MI: forward + decorator combined", "[inheritance][mi][forward][decorator]") {
    struct SwimDecorator : ISwimmable {
        librtdi::decorated_ptr<ISwimmable> inner_;
        explicit SwimDecorator(librtdi::decorated_ptr<ISwimmable> inner) : inner_(std::move(inner)) {}
        int swim_speed() const override { return inner_->swim_speed() * 3; }
    };

    librtdi::registry reg;
    reg.add_singleton<Duck, Duck>();
    reg.forward<ISwimmable, Duck>();
    reg.decorate<ISwimmable, SwimDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(swimmer.swim_speed() == 15); // 5 * 3

    // Original Duck is not decorated via ISwimmable
    auto& duck = r->get<Duck>();
    REQUIRE(duck.swim_speed() == 5);
}

TEST_CASE("MI: forward to two bases + decorate each independently", "[inheritance][mi][forward][decorator]") {
    struct AnimalDecorator : IAnimal {
        librtdi::decorated_ptr<IAnimal> inner_;
        explicit AnimalDecorator(librtdi::decorated_ptr<IAnimal> inner) : inner_(std::move(inner)) {}
        std::string species() const override { return "fancy_" + inner_->species(); }
    };

    struct SwimDecorator : ISwimmable {
        librtdi::decorated_ptr<ISwimmable> inner_;
        explicit SwimDecorator(librtdi::decorated_ptr<ISwimmable> inner) : inner_(std::move(inner)) {}
        int swim_speed() const override { return inner_->swim_speed() * 2; }
    };

    librtdi::registry reg;
    reg.add_singleton<Duck, Duck>();
    reg.forward<IAnimal, Duck>();
    reg.forward<ISwimmable, Duck>();
    reg.decorate<IAnimal, AnimalDecorator>();
    reg.decorate<ISwimmable, SwimDecorator>();
    auto r = reg.build({.validate_on_build = false});

    auto& animal = r->get<IAnimal>();
    REQUIRE(animal.species() == "fancy_duck");

    auto& swimmer = r->get<ISwimmable>();
    REQUIRE(swimmer.swim_speed() == 10); // 5 * 2

    // Original Duck unchanged
    auto& duck = r->get<Duck>();
    REQUIRE(duck.species() == "duck");
    REQUIRE(duck.swim_speed() == 5);
}

// ===============================================================
// Virtual Inheritance: Basic
// ===============================================================

TEST_CASE("VI: simple virtual inheritance chain", "[inheritance][vi]") {
    librtdi::registry reg;
    reg.add_singleton<VBase, VLeaf>();
    auto r = reg.build({.validate_on_build = false});

    auto& base = r->get<VBase>();
    REQUIRE(base.id() == 42);
}

TEST_CASE("VI: transient via virtual base", "[inheritance][vi]") {
    librtdi::registry reg;
    reg.add_transient<VBase, VLeaf>();
    auto r = reg.build({.validate_on_build = false});

    auto a = r->create<VBase>();
    auto b = r->create<VBase>();
    REQUIRE(a->id() == 42);
    REQUIRE(b->id() == 42);
    REQUIRE(a.get() != b.get());
}

TEST_CASE("VI: register as intermediate virtual base", "[inheritance][vi]") {
    librtdi::registry reg;
    reg.add_singleton<VMid, VLeaf>();
    auto r = reg.build({.validate_on_build = false});

    auto& mid = r->get<VMid>();
    REQUIRE(mid.id() == 42);
}

// ===============================================================
// Diamond Inheritance
// ===============================================================

TEST_CASE("VI: diamond - resolve via IShape (virtual root)", "[inheritance][vi][diamond]") {
    librtdi::registry reg;
    reg.add_singleton<IShape, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& shape = r->get<IShape>();
    REQUIRE(shape.shape_name() == "rect");
}

TEST_CASE("VI: diamond - resolve via IDrawable (virtual arm)", "[inheritance][vi][diamond]") {
    librtdi::registry reg;
    reg.add_singleton<IDrawable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& drawable = r->get<IDrawable>();
    REQUIRE(drawable.shape_name() == "rect");
}

TEST_CASE("VI: diamond - resolve via IResizable (virtual arm)", "[inheritance][vi][diamond]") {
    librtdi::registry reg;
    reg.add_singleton<IResizable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& resizable = r->get<IResizable>();
    REQUIRE(resizable.shape_name() == "rect");
}

TEST_CASE("VI: diamond - register under all three interfaces", "[inheritance][vi][diamond]") {
    librtdi::registry reg;
    reg.add_singleton<IShape, DrawableResizableRect>();
    reg.add_singleton<IDrawable, DrawableResizableRect>();
    reg.add_singleton<IResizable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    REQUIRE(r->get<IShape>().shape_name() == "rect");
    REQUIRE(r->get<IDrawable>().shape_name() == "rect");
    REQUIRE(r->get<IResizable>().shape_name() == "rect");
}

TEST_CASE("VI: diamond - forward from concrete to virtual root", "[inheritance][vi][diamond][forward]") {
    librtdi::registry reg;
    reg.add_singleton<DrawableResizableRect, DrawableResizableRect>();
    reg.forward<IShape, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& shape = r->get<IShape>();
    auto& concrete = r->get<DrawableResizableRect>();
    REQUIRE(shape.shape_name() == "rect");
    REQUIRE(static_cast<IShape*>(&concrete) == &shape);
}

TEST_CASE("VI: diamond - forward from concrete to virtual arm (IDrawable)", "[inheritance][vi][diamond][forward]") {
    librtdi::registry reg;
    reg.add_singleton<DrawableResizableRect, DrawableResizableRect>();
    reg.forward<IDrawable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& drawable = r->get<IDrawable>();
    auto& concrete = r->get<DrawableResizableRect>();
    REQUIRE(drawable.shape_name() == "rect");
    REQUIRE(static_cast<IDrawable*>(&concrete) == &drawable);
}

TEST_CASE("VI: diamond - forward to all three interfaces", "[inheritance][vi][diamond][forward]") {
    librtdi::registry reg;
    reg.add_singleton<DrawableResizableRect, DrawableResizableRect>();
    reg.forward<IShape, DrawableResizableRect>();
    reg.forward<IDrawable, DrawableResizableRect>();
    reg.forward<IResizable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    auto& concrete = r->get<DrawableResizableRect>();
    auto& shape = r->get<IShape>();
    auto& drawable = r->get<IDrawable>();
    auto& resizable = r->get<IResizable>();

    REQUIRE(shape.shape_name() == "rect");
    REQUIRE(static_cast<IShape*>(&concrete) == &shape);
    REQUIRE(static_cast<IDrawable*>(&concrete) == &drawable);
    REQUIRE(static_cast<IResizable*>(&concrete) == &resizable);
}

// ===============================================================
// VI: Forward transient
// ===============================================================

TEST_CASE("VI: forward transient via virtual base", "[inheritance][vi][forward]") {
    librtdi::registry reg;
    reg.add_transient<VLeaf, VLeaf>();
    reg.forward<VBase, VLeaf>();
    auto r = reg.build({.validate_on_build = false});

    auto ptr = r->create<VBase>();
    REQUIRE(ptr != nullptr);
    REQUIRE(ptr->id() == 42);
}

TEST_CASE("VI: diamond forward transient - no crash on delete", "[inheritance][vi][diamond][forward]") {
    librtdi::registry reg;
    reg.add_transient<DrawableResizableRect, DrawableResizableRect>();
    reg.forward<IShape, DrawableResizableRect>();
    reg.forward<IDrawable, DrawableResizableRect>();
    auto r = reg.build({.validate_on_build = false});

    // Creating and destroying transient via virtual base must not crash
    {
        auto shape = r->create<IShape>();
        REQUIRE(shape->shape_name() == "rect");
    }
    {
        auto drawable = r->create<IDrawable>();
        REQUIRE(drawable->shape_name() == "rect");
    }
}

// ===============================================================
// VI: Collection
// ===============================================================

TEST_CASE("VI: collection via virtual base", "[inheritance][vi]") {
    librtdi::registry reg;
    reg.add_collection<VBase, VLeaf>(librtdi::lifetime_kind::singleton);
    reg.add_collection<VBase, VMid>(librtdi::lifetime_kind::singleton);
    auto r = reg.build({.validate_on_build = false});

    auto all = r->get_all<VBase>();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]->id() == 42);
    REQUIRE(all[1]->id() == 99);
}

// ===============================================================
// MI: Dependency injection through non-first base
// ===============================================================

TEST_CASE("MI: auto-wiring with MI dependency", "[inheritance][mi]") {
    struct IEngine {
        virtual ~IEngine() = default;
        virtual int horsepower() const = 0;
    };

    struct ITransmission {
        virtual ~ITransmission() = default;
        virtual int gears() const = 0;
    };

    struct V8 : IEngine, ITransmission {
        int horsepower() const override { return 400; }
        int gears() const override { return 6; }
    };

    struct Car {
        ITransmission& trans_;
        explicit Car(ITransmission& t) : trans_(t) {}
        int gears() const { return trans_.gears(); }
    };

    librtdi::registry reg;
    reg.add_singleton<ITransmission, V8>();
    reg.add_singleton<Car, Car>(librtdi::deps<ITransmission>);
    auto r = reg.build();

    auto& car = r->get<Car>();
    REQUIRE(car.gears() == 6);
}
