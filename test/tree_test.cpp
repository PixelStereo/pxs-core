#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/osc_bridge.h>
#include <pxs/core/tree/tree.h>

#include "check.h"

#include <cmath>
#include <limits>

using namespace pxs::core;
using namespace pxs::core::tree;
using Type = ParamInfo::Type;

namespace {

// A parameter whose value lives in a cell
struct Cell {
    Value value;
    int writes = 0;
};

std::unique_ptr<Parameter> make(Cell &cell, const std::string &path, Type type)
{
    auto p = std::make_unique<Parameter>(path, "Cat \xE2\x80\xBA " + path, type);
    p->bind([&cell] { return cell.value; }, [&cell](const Value &v) { cell.value = v; ++cell.writes; });
    return p;
}

osc::Message message(const std::string &address, std::initializer_list<osc::Value> args)
{
    osc::Message m;
    m.address = address;
    m.args = args;
    return m;
}

void floatWritesAreHeldWithinTheLimits()
{
    Cell c;
    auto p = make(c, "level", Type::Float);
    p->range(0, 2).limits(0.5, 1.5);
    CHECK(p->setValue(Value(1.0)) && c.value == Value(1.0));
    CHECK(p->setValue(Value(9.0)) && c.value == Value(1.5));
    CHECK(p->setValue(Value(-9.0)) && c.value == Value(0.5));
    CHECK(p->setValue(Value("1.25")) && c.value == Value(1.25)); // a number in text
    CHECK(!p->setValue(Value("loud")) && !p->setValue(Value()) && !p->setValue(Value(std::nan(""))));
    CHECK(!p->setValue(Value(std::numeric_limits<double>::infinity())));
    CHECK_EQ(c.writes, 4);
}

void intWritesGoToTheNearestValue()
{
    Cell c;
    auto p = make(c, "n", Type::Int);
    p->range(-5, 5);
    CHECK(p->setValue(Value(2.4)) && c.value == Value(2));
    CHECK(p->setValue(Value(2.5)) && c.value == Value(3));
    CHECK(p->setValue(Value(-2.5)) && c.value == Value(-3));
    CHECK(p->setValue(Value(99)) && c.value == Value(5));
    CHECK(!p->setValue(Value(std::numeric_limits<double>::infinity())));

    Cell d;
    auto q = make(d, "m", Type::Int);
    q->values({1, 4, 16});
    CHECK(q->setValue(Value(5.0)) && d.value == Value(4));
    CHECK(q->setValue(Value(12)) && d.value == Value(16));
    CHECK(q->setValue(Value(-100)) && d.value == Value(1));
    CHECK_EQ(q->info().min, 1.0);
    CHECK_EQ(q->info().max, 16.0);
}

void boolsChoicesAndTexts()
{
    Cell b;
    auto pb = make(b, "on", Type::Bool);
    CHECK(pb->setValue(Value(true)) && b.value == Value(true));
    CHECK(pb->setValue(Value("false")) && b.value == Value(false));
    CHECK(pb->setValue(Value(2)) && b.value == Value(true));
    CHECK_EQ(pb->asNumber(), 1.0);

    Cell c;
    auto pc = make(c, "mode", Type::Choice);
    pc->choices({"red", "green", "blue"});
    CHECK(pc->setValue(Value("Green")) && c.value == Value("green")); // case-insensitive, stored as the key
    CHECK(!pc->setValue(Value("yellow")) && !pc->setValue(Value(1)));
    c.value = Value("blue");
    CHECK_EQ(pc->asNumber(), 2.0);
    c.value = Value("nothing");
    CHECK(std::isnan(pc->asNumber()));

    Cell t;
    auto pt = make(t, "name", Type::Text);
    CHECK(pt->setValue(Value("hello")) && t.value == Value("hello"));
    CHECK(pt->setValue(Value(12)) && t.value == Value("12"));
    CHECK(pt->setValue(Value(true)) && t.value == Value("true"));

    Cell g;
    auto pg = make(g, "go", Type::Trigger);
    CHECK(pg->setValue(Value(1)) && g.value.isNil() && g.writes == 1);
}

void accessAndDeclaration()
{
    Cell c;
    auto ro = make(c, "ro", Type::Float);
    ro->readOnly();
    CHECK(!ro->setValue(Value(1.0)) && !ro->info().animatable && !ro->info().action);

    Cell w;
    auto wo = make(w, "wo", Type::Float);
    wo->writeOnly();
    CHECK(wo->value().isNil() && wo->info().action && wo->setValue(Value(0.5)));

    Cell f;
    auto p = make(f, "x", Type::Float);
    CHECK(p->info().animatable && p->isStored() && p->info().stored && p->info().presettable);
    p->transient();
    CHECK(!p->isStored() && p->info().presettable);
    CHECK_EQ(p->info().name(), std::string("x"));
    auto named = make(f, "spatial/rotation", Type::Float);
    CHECK_EQ(named->info().name(), std::string("spatial/rotation")); // the label is "Cat › spatial/rotation"
    Parameter plain("a", "Spatial \xE2\x80\xBA Rotation (\xC2\xB0)", Type::Float);
    CHECK_EQ(plain.info().name(), std::string("Rotation (\xC2\xB0)"));
    const Parameter under = plain.prefixed("fx/blur/");
    CHECK_EQ(under.path(), std::string("fx/blur/a"));
}

void defaultsAndReset()
{
    Cell c;
    c.value = Value(0.9);
    auto p = make(c, "d", Type::Float);
    p->byDefault(Value(0.25));
    CHECK(p->reset() && c.value == Value(0.25));
    int calls = 0;
    p->byDefault(Parameter::Getter([&] { ++calls; return Value(0.75); }));
    CHECK(p->info().defaultValue == Value(0.75) && calls == 1); // resolved when asked: it depends on where the holder is
}

void storedValues()
{
    Cell f, i, b, s, g;
    f.value = Value(0.5f);
    i.value = Value(7.4);
    b.value = Value("true");
    s.value = Value(12);
    CHECK(make(f, "a", Type::Float)->stored() == Value(0.5));
    CHECK(make(i, "b", Type::Int)->stored() == Value(7));
    CHECK(make(b, "c", Type::Bool)->stored() == Value(true));
    CHECK(make(s, "d", Type::Text)->stored() == Value("12"));
    CHECK(make(g, "e", Type::Trigger)->stored().isNil());
    Cell l;
    auto p = make(l, "f", Type::Float);
    CHECK(p->setStored(Value(0.3)) && !p->setStored(Value()));
}

void feedIsToldOfWrites()
{
    struct Rec : ChangeFeed::Observer {
        std::vector<std::string> log;
        void parameterWritten(std::uint64_t h, std::string_view p) override { log.push_back(std::to_string(h) + " " + std::string(p)); }
        void layerChanged(std::uint64_t) override {}
        void addressChanged(std::string_view) override {}
    } rec;
    ChangeFeed feed;
    feed.observe(&rec);
    Cell c;
    auto p = make(c, "opacity", Type::Float);
    p->holder(7).feed(&feed);
    CHECK(p->setValue(Value(0.5)));
    CHECK(!p->setValue(Value("loud"))); // a refused write says nothing
    CHECK_EQ(rec.log.size(), std::size_t(1));
    CHECK_EQ(rec.log[0], std::string("7 opacity"));
    feed.ignore(&rec);
}

void namesAreMadeSafeAndFound()
{
    Tree t;
    Node &n = t.add("/a b/c#d/e?f");
    CHECK_EQ(n.path(), std::string("/a_b/c_d/e_f"));
    CHECK(t.find("/a b/c#d/e?f") == &n);
    CHECK(t.find("a_b/c_d/e_f") == &n); // with or without the leading '/'
    CHECK(t.find("/a_b/missing") == nullptr);
    CHECK_EQ(t.root().path(), std::string("/"));
    CHECK(n.isContainer()); // nothing declared yet: a node with no type and no access is a container
    CHECK(t.remove("/a_b/c_d") && !t.find("/a_b/c_d/e_f") && !t.remove("/") && !t.remove("/nope"));
}

void aValueCanHoldNodes()
{
    Tree t;
    Node &tint = t.addSignal("/color/tint", "f"); // a node with a value of its own...
    t.add("/color/tint/enable").type = "T";       // ...that holds another one
    CHECK(!tint.isContainer() && tint.hasValue() && tint.hasListedChildren());
    CHECK(tint.kind() == Kind::Signal);
    CHECK(t.find("/color")->isContainer() && t.find("/color")->kind() == Kind::Container);
    const std::string json = describeJson(t, "/color/tint");
    CHECK(json.find("\"TYPE\"") != std::string::npos);
    CHECK(json.find("\"CONTENTS\"") != std::string::npos);
}

void theTreeIsChecked()
{
    Tree t;
    CHECK(t.check().empty());
    Node &level = t.add("/a/level");
    level.type = "f";
    level.access = 3; // numeric, readable, writable, and nothing else
    const std::vector<std::string> problems = t.check();
    CHECK_EQ(problems.size(), std::size_t(3)); // no VALUE, no RANGE, no DEFAULT_VALUE
    level.get = [] { return ValueList{Value(0.5)}; };
    level.range = {ArgRange{Value(0.0), Value(1.0), {}, {}}};
    level.defaultValue = [] { return ValueList{Value(0.5)}; };
    CHECK(t.check().empty());
    t.add("/a/name").type = "s"; // a text: no range or default asked
    t.find("/a/name")->access = 1;
    t.find("/a/name")->get = [] { return ValueList{Value("x")}; };
    CHECK(t.check().empty());
}

void structureIsTold()
{
    Tree t;
    std::vector<std::string> log;
    t.setStructureObserver([&](const std::string &p, Tree::Change c) { log.push_back(p + " " + std::to_string(int(c))); });
    t.add("/x/y/z");
    t.add("/x/y/z"); // already there: nothing to tell
    t.add("/x/w");
    t.remove("/x/y");
    t.clear();
    CHECK_EQ(log.size(), std::size_t(4));
    CHECK_EQ(log[0], std::string("/x 0")); // the highest node made
    CHECK_EQ(log[1], std::string("/x/w 0"));
    CHECK_EQ(log[2], std::string("/x/y 1"));
    CHECK_EQ(log[3], std::string("/ 2"));
}

void parametersActionsSignals()
{
    Tree t;
    Cell level, mode;
    {
        auto p = make(level, "level", Type::Float);
        p->range(0, 4).byDefault(Value(1.0)).unit("gain.db");
        Node &n = t.add("/layer/level", std::move(p));
        CHECK(n.kind() == Kind::Parameter && n.type == "f" && n.access == 3 && n.clip == "both" && n.label == "level");
        CHECK(n.range.size() == 1 && n.range[0].min == Value(0.0) && n.range[0].max == Value(4.0));
        CHECK(n.unit == std::vector<std::string>{"gain.db"} && n.extendedType.empty());
        CHECK(n.reset && n.defaultValue && n.defaultValue().front() == Value(1.0));
    }
    {
        auto p = make(mode, "mode", Type::Choice);
        p->choices({"a", "b"}).colorComponent(1);
        Node &n = t.add("/layer/mode", std::move(p));
        CHECK(n.type == "s" && n.range[0].vals.size() == 2 && n.range[0].vals[1] == Value("b") && !n.range[0].min);
        CHECK(n.extendedType == std::vector<std::string>{"color.rgb.g"});
    }
    Cell trig;
    {
        Node &n = t.add("/layer/go", make(trig, "go", Type::Trigger));
        CHECK(n.kind() == Kind::Action && n.type == "N" && n.access == 2 && !n.get && !n.reset);
    }
    Cell wo;
    {
        auto p = make(wo, "wo", Type::Float);
        p->writeOnly();
        CHECK(t.add("/layer/wo", std::move(p)).kind() == Kind::Action); // nobody can read it back: a message
    }
    int hits = 0;
    Node &action = t.addAction("/act/two", "ff", [&](const ValueList &a) { ++hits; return a.size() == 2; }, "does a thing");
    CHECK(action.kind() == Kind::Action && action.access == 2 && action.description == "does a thing" && action.label == "two");
    CHECK(t.addAction("/act/none", "", [](const ValueList &) { return true; }).type == "N");
    CHECK(t.addSignal("/sig/beat", "f").kind() == Kind::Signal);
    CHECK(t.addSignal("/sig/blank").type == "N");

    CHECK_EQ(t.apply(message("/layer/level", {osc::Value(2.5)})), 1);
    CHECK(level.value == Value(2.5));
    CHECK_EQ(t.apply(message("/layer/level", {osc::Value(std::int32_t(9))})), 1);
    CHECK(level.value == Value(4.0));                                    // held within the limits
    CHECK_EQ(t.apply(message("/layer/level", {osc::Value(std::string("loud"))})), 0);
    CHECK_EQ(t.apply(message("/layer/level", {osc::Value(std::string("1.5"))})), 1);
    CHECK_EQ(t.apply(message("/layer/mode", {osc::Value(std::string("B"))})), 1);
    CHECK(mode.value == Value("b"));
    CHECK_EQ(t.apply(message("/layer/mode", {osc::Value(std::int32_t(0))})), 1); // by its place among the keys
    CHECK(mode.value == Value("a"));
    CHECK_EQ(t.apply(message("/layer/mode", {osc::Value(std::int32_t(5))})), 0);
    CHECK_EQ(t.apply(message("/layer/mode", {})), 0); // nothing to choose with (the first key was chosen in pxs 0.x: a bug)
    CHECK_EQ(t.apply(message("/layer/go", {})), 1);
    CHECK_EQ(trig.writes, 1);
    CHECK_EQ(t.apply(message("/act/two", {osc::Value(1.0f), osc::Value(2.0f)})), 1);
    CHECK_EQ(t.apply(message("/act/two", {osc::Value(1.0f)})), 0);
    CHECK_EQ(hits, 2);
    CHECK_EQ(t.apply(message("/sig/beat", {osc::Value(1.0f)})), 0); // a signal is not written
    CHECK_EQ(t.apply(message("/nowhere", {})), 0);
    CHECK_EQ(t.apply(message("/layer/*", {osc::Value(std::int32_t(1))})), 4); // level, mode (key 1 is "b"), go (a trigger takes anything), wo
}

void patternsReachSeveralNodes()
{
    Tree t;
    Cell a, b, c;
    t.add("/layer/1/opacity", make(a, "o1", Type::Float));
    t.add("/layer/2/opacity", make(b, "o2", Type::Float));
    t.add("/layer/10/opacity", make(c, "o10", Type::Float));
    CHECK_EQ(t.match("/layer/*/opacity").size(), std::size_t(3));
    CHECK_EQ(t.match("/layer/?/opacity").size(), std::size_t(2));
    CHECK_EQ(t.match("/layer/{1,10}/opacity").size(), std::size_t(2));
    CHECK_EQ(t.match("/layer/[12]").size(), std::size_t(2)); // the containers too
    CHECK_EQ(t.match("/").size(), std::size_t(1));
    CHECK_EQ(t.apply(message("/layer/*/opacity", {osc::Value(0.5)})), 3);
    CHECK(a.value == Value(0.5) && b.value == Value(0.5) && c.value == Value(0.5));
}

void lockedNodesRefuseWrites()
{
    Tree t;
    Cell c;
    t.add("/x", make(c, "x", Type::Float));
    CHECK(t.setLocked("/x", true) && !t.setLocked("/nope", true));
    CHECK_EQ(t.apply(message("/x", {osc::Value(1.0)})), 0);
    CHECK(!t.reset("/x"));
    CHECK(t.setLocked("/x", false));
    CHECK_EQ(t.apply(message("/x", {osc::Value(1.0)})), 1);

    // locks are saved by address, and one set before its node exists waits for it
    CHECK(t.setLocked("/x", true));
    CHECK(t.lockedPaths() == std::vector<std::string>{"/x"});
    Tree u;
    u.setLockedPaths({"/x", "/later"});
    Cell d, e;
    Node &x = u.add("/x", make(d, "x", Type::Float));
    CHECK(x.locked); // it existed? no: the lock waited and was applied when the node was made
    Node &later = u.add("/later", make(e, "later", Type::Float));
    CHECK(later.locked);
    CHECK(u.lockedPaths().size() == 2);
}

void observersSeeEveryWriteOnce()
{
    Tree t;
    Cell c;
    t.add("/x", make(c, "x", Type::Float));
    t.addSignal("/s", "f");
    std::vector<std::string> log;
    const int id = t.addObserver([&](const Node &n, const ValueList &v) { log.push_back(n.path() + " " + (v.empty() ? "-" : v.front().text())); });
    t.apply(message("/x", {osc::Value(0.5)}));
    t.apply(message("/x", {osc::Value(std::string("no"))})); // refused: nobody hears
    t.fire("/s", {Value(3.5f)});
    t.changed("/x");
    t.setLocked("/x", true);
    CHECK_EQ(log.size(), std::size_t(4));
    CHECK_EQ(log[0], std::string("/x 0.5"));
    CHECK_EQ(log[1], std::string("/s 3.5"));
    CHECK_EQ(log[2], std::string("/x 0.5"));
    CHECK_EQ(log[3], std::string("/x 0.5")); // a lock is told with the value held
    t.removeObserver(id);
    t.apply(message("/x", {osc::Value(0.25)}));
    CHECK_EQ(log.size(), std::size_t(4));
}

void anObserverMaySayChangedItself()
{
    // A write that tells the others itself (a bound widget) is not told twice
    Tree t;
    Cell c;
    Node &n = t.add("/x", make(c, "x", Type::Float));
    int told = 0;
    t.addObserver([&](const Node &, const ValueList &) { ++told; });
    const auto original = n.set;
    n.set = [&](const ValueList &a) {
        const bool ok = original(a);
        if (ok) t.changed("/x");
        return ok;
    };
    t.apply(message("/x", {osc::Value(0.5)}));
    CHECK_EQ(told, 1);
}

void outputAndMessages()
{
    Tree t;
    std::vector<osc::Message> out;
    t.setOutput([&](const osc::Message &m) { out.push_back(m); });
    Cell level, on;
    t.add("/level", make(level, "level", Type::Float));
    t.add("/on", make(on, "on", Type::Bool));
    t.addSignal("/beat", "ff");
    t.addSignal("/blank");
    level.value = Value(0.5);
    on.value = Value(true);

    const osc::Message m = Tree::message(*t.find("/level"));
    CHECK_EQ(m.address, std::string("/level"));
    CHECK_EQ(m.types, std::string("f"));
    CHECK(m.args.size() == 1 && std::holds_alternative<float>(m.args[0]) && std::get<float>(m.args[0]) == 0.5f);
    const osc::Message sw = Tree::message(*t.find("/on"));
    CHECK(sw.types == "T" && sw.args.empty());
    on.value = Value(false);
    CHECK(Tree::message(*t.find("/on")).types == "F");

    CHECK(t.changed("/level") && out.size() == 1 && out[0].address == "/level");
    CHECK(!t.changed("/beat") && !t.changed("/missing")); // a signal has no state to feed back
    CHECK(t.fire("/beat", {Value(1.5), Value(2)}));
    CHECK(out.size() == 2 && out[1].types == "ff" && std::get<float>(out[1].args[0]) == 1.5f && std::get<float>(out[1].args[1]) == 2.0f);
    CHECK(t.fire("/blank"));
    CHECK(out.size() == 3 && out[2].types.empty() && out[2].args.empty());
    CHECK(!t.fire("/level") && !t.fire("/missing"));
}

void conversionsToTheWire()
{
    CHECK(std::get<std::int32_t>(toOsc('i', Value(2.5))) == 3);
    CHECK(std::get<std::int32_t>(toOsc('i', Value(1e20))) == std::numeric_limits<std::int32_t>::max()); // held, not wrapped
    CHECK(std::get<std::int32_t>(toOsc('i', Value(-1e20))) == std::numeric_limits<std::int32_t>::min());
    CHECK(std::get<std::int32_t>(toOsc('i', Value("12"))) == 12);
    CHECK(std::get<std::int32_t>(toOsc('i', Value("abc"))) == 0 && std::get<std::int32_t>(toOsc('i', Value(std::nan("")))) == 0);
    CHECK(std::get<std::int64_t>(toOsc('h', Value(std::int64_t(1) << 40))) == (std::int64_t(1) << 40));
    CHECK(std::get<float>(toOsc('f', Value(0.5))) == 0.5f);
    CHECK(std::get<double>(toOsc('d', Value(true))) == 1.0);
    CHECK(std::get<std::string>(toOsc('s', Value(7))) == "7");
    CHECK(std::get<std::uint32_t>(toOsc('c', Value("\xC3\xA9"))) == 0xE9u); // the first character of the text
    CHECK(std::get<std::uint32_t>(toOsc('r', Value(std::int64_t(4286595264LL)))) == 4286595264u);
    CHECK(std::get<bool>(toOsc('T', Value(1))) && !std::get<bool>(toOsc('F', Value(0))));
    CHECK(std::holds_alternative<std::monostate>(toOsc('N', Value(1))));
    CHECK(std::get<osc::Blob>(toOsc('b', Value(Blob{1, 2}))) == osc::Blob({1, 2}));

    CHECK(fromOsc(osc::Value(std::int32_t(3))) == Value(3));
    CHECK(fromOsc(osc::Value(std::uint32_t(4000000000u))) == Value(std::int64_t(4000000000LL)));
    CHECK(fromOsc(osc::Value(std::monostate{})).isNil());
    CHECK(fromOsc(osc::Value(osc::Blob{9})) == Value(Blob{9}));

    // a vector is as many floats; the brackets of an array are no argument; a missing value is the tag's zero
    const auto args = toOscArguments("fff", {Value(Vec3{1, 2, 3})});
    CHECK(args.size() == 3 && std::get<float>(args[2]) == 3.0f);
    CHECK(toOscArguments("[ff]i", {Value(1.0f), Value(2.0f), Value(7)}).size() == 3);
    CHECK(toOscArguments("ff", {Value(1.0f)}).size() == 2);
}

void nodesAreTheTreesOwn()
{
    Tree t;
    Cell c;
    Node &n = t.add("/a/b", make(c, "b", Type::Float));
    CHECK(n.parent() && n.parent()->name() == "a" && n.parent()->parent() == &t.root());
    CHECK(n.parameter() != nullptr && n.animatable() && n.displayLabel() == "b");
    n.label.clear();
    CHECK_EQ(n.displayLabel(), std::string("b"));
    CHECK(t.root().child("a") == n.parent() && t.root().child("zz") == nullptr);
    t.clear();
    CHECK(t.root().children().empty());
}

void attachedMethodsAreReachedButNotListed()
{
    Tree t;
    Cell level;
    auto p = make(level, "level", Type::Float);
    p->range(0, 4).byDefault(Value(1.0));
    t.add("/layer/level", std::move(p));
    int resets = 0;
    Node &reset = t.addAction("/layer/level/reset", "N", [&resets](const ValueList &) { ++resets; return true; }, "Reset level to default");
    reset.attached = true;
    Node *level_node = t.find("/layer/level");
    CHECK(level_node->kind() == Kind::Parameter && !level_node->isContainer() && !level_node->hasListedChildren());
    CHECK(t.find("/layer/level/reset") == &reset && t.match("/layer/*/reset").size() == 1);
    CHECK(t.apply("/layer/level/reset", {}) == 1 && resets == 1);
    const std::string json = describeJson(t, "/layer/level");
    CHECK(json.find("\"VALUE\"") != std::string::npos && json.find("\"CONTENTS\"") == std::string::npos);
    CHECK(describeJson(t, "/layer").find("\"CONTENTS\":{\"level\"") != std::string::npos);
    // A node with a listed child is a container, as before
    Cell other;
    t.add("/layer/other/inner", make(other, "inner", Type::Float));
    CHECK(t.find("/layer/other")->isContainer());
}

void fireIfChangedSparesTheListeners()
{
    // A valued signal fired again and again with the same value from a tick: the listeners are told only when it differs
    Tree t;
    std::vector<osc::Message> out;
    t.setOutput([&](const osc::Message &m) { out.push_back(m); });
    t.addSignal("/level", "f");
    t.addSignal("/beat");
    CHECK(t.fireIfChanged("/level", {Value(0.5)}));
    CHECK(!t.fireIfChanged("/level", {Value(0.5)})); // the same: not again
    CHECK(t.fireIfChanged("/level", {Value(0.75)}));
    CHECK_EQ(out.size(), std::size_t(2));
    CHECK(t.fireIfChanged("/beat"));
    CHECK(t.fireIfChanged("/beat")); // an impulse: every one of them is one
    CHECK(!t.fireIfChanged("/missing", {Value(1.0)}));
    CHECK_EQ(out.size(), std::size_t(4));
    // What it fired last is forgotten when the node goes: it comes back firing
    t.remove("/level");
    t.addSignal("/level", "f");
    CHECK(t.fireIfChanged("/level", {Value(0.5)}));
    t.clear();
    CHECK(!t.fireIfChanged("/missing", {Value(1.0)}));
}

} // namespace

int main()
{
    floatWritesAreHeldWithinTheLimits();
    intWritesGoToTheNearestValue();
    boolsChoicesAndTexts();
    accessAndDeclaration();
    defaultsAndReset();
    storedValues();
    feedIsToldOfWrites();
    namesAreMadeSafeAndFound();
    aValueCanHoldNodes();
    theTreeIsChecked();
    structureIsTold();
    parametersActionsSignals();
    patternsReachSeveralNodes();
    lockedNodesRefuseWrites();
    observersSeeEveryWriteOnce();
    anObserverMaySayChangedItself();
    outputAndMessages();
    fireIfChangedSparesTheListeners();
    conversionsToTheWire();
    nodesAreTheTreesOwn();
    attachedMethodsAreReachedButNotListed();
    return pxs_test::finish("tree");
}
