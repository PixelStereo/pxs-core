#include <pxs/core/tree/preset.h>

#include "check.h"

using namespace pxs::core;
using namespace pxs::core::tree;
using Type = ParamInfo::Type;

namespace {

struct Cell {
    Value value;
    int writes = 0;
};

std::unique_ptr<Parameter> make(Cell &cell, const std::string &path, Type type)
{
    auto p = std::make_unique<Parameter>(path, path, type);
    p->bind([&cell] { return cell.value; }, [&cell](const Value &v) { cell.value = v; ++cell.writes; });
    return p;
}

struct Show {
    Tree tree;
    Cell level, mode, on, shown, name, go;
    std::vector<std::string> hits;
    Show()
    {
        level.value = Value(0.25);
        mode.value = Value("a");
        on.value = Value(true);
        name.value = Value("Title");
        tree.add("/level", make(level, "level", Type::Float));
        auto m = make(mode, "mode", Type::Choice);
        m->choices({"a", "b"});
        tree.add("/mode", std::move(m));
        tree.add("/on", make(on, "on", Type::Bool));
        auto transient = make(shown, "shown", Type::Float);
        transient->transient(); // a state of the show: not stored, so not held by default
        tree.add("/shown", std::move(transient));
        tree.add("/name", make(name, "name", Type::Text));
        tree.add("/go", make(go, "go", Type::Trigger));
        tree.addAction("/rewind", "N", [this](const ValueList &) { hits.push_back("rewind"); return true; });
        tree.addAction("/cue", "i", [this](const ValueList &a) { hits.push_back("cue " + std::to_string(int(a.front().number().value_or(-1)))); return true; });
        Node &hand = tree.add("/hand"); // filled in by hand: read and written
        hand.type = "f";
        hand.access = 3;
        hand.get = [this] { return ValueList{Value(handValue)}; };
        hand.set = [this](const ValueList &a) { handValue = a.front().number().value_or(0.0); return true; };
    }
    double handValue = 7.5;
};

void captureHoldsWhatIsStored()
{
    Show s;
    const Preset p = capture(s.tree);
    // level, mode, on, name (stored parameters) and hand (filled in by hand): not shown (transient), not go (a trigger)
    CHECK_EQ(p.values.size(), std::size_t(5));
    CHECK_EQ(p.values.at("/level"), std::string("[0.25]"));
    CHECK_EQ(p.values.at("/mode"), std::string("[\"a\"]"));
    CHECK_EQ(p.values.at("/on"), std::string("[true]"));
    CHECK_EQ(p.values.at("/name"), std::string("[\"Title\"]"));
    CHECK_EQ(p.values.at("/hand"), std::string("[7.5]"));
    CHECK(!p.values.count("/shown") && !p.values.count("/go") && !p.values.count("/rewind"));
    CHECK(!p.isEmpty() && Preset().isEmpty());

    const Preset only = capture(s.tree, {"/level", "/shown", "/missing", "/rewind"}); // naming a node asks for it, if it can be held
    CHECK_EQ(only.values.size(), std::size_t(1));
}

void recallPutsTheValuesBack()
{
    Show s;
    const Preset p = capture(s.tree, {}, {{"/rewind", ""}, {"/cue", "[3]"}});
    s.level.value = Value(0.9);
    s.mode.value = Value("b");
    s.on.value = Value(false);
    s.name.value = Value("x");
    s.handValue = 1;
    const RecallResult r = recall(s.tree, p);
    CHECK_EQ(r.applied, 5);
    CHECK_EQ(r.actions, 2);
    CHECK_EQ(r.locked, 0);
    CHECK_EQ(r.missing, 0);
    CHECK(s.level.value == Value(0.25) && s.mode.value == Value("a") && s.on.value == Value(true) && s.name.value == Value("Title"));
    CHECK_EQ(s.handValue, 7.5);
    CHECK(s.hits == std::vector<std::string>({"rewind", "cue 3"})); // the actions come after the values, in order
}

void lockedAndMissingAreCounted()
{
    Show s;
    Preset p = capture(s.tree, {}, {{"/rewind", ""}, {"/nowhere", ""}, {"/level", ""}}); // /level is no action
    p.values["/ghost"] = "[1.0]";
    p.values["/go"] = "[1.0]"; // not holdable
    s.tree.setLocked("/level", true);
    s.tree.setLocked("/rewind", true);
    s.level.value = Value(0.9);
    const RecallResult r = recall(s.tree, p);
    CHECK(s.level.value == Value(0.9)); // a locked parameter keeps its value
    CHECK_EQ(r.locked, 2);
    CHECK_EQ(r.applied, 4);
    CHECK_EQ(r.actions, 0);
    CHECK_EQ(r.missing, 4); // /ghost, /go, /nowhere, /level as an action
    CHECK(s.hits.empty());
}

void aValueThatDoesNotFitIsCountedAsMissing()
{
    Show s;
    Preset p;
    p.values["/level"] = "[\"loud\"]";
    p.values["/mode"] = "[\"yellow\"]";
    p.values["/on"] = "[]";
    const RecallResult r = recall(s.tree, p);
    CHECK_EQ(r.applied, 1); // /on with no argument writes false, as OSC does
    CHECK_EQ(r.missing, 2);
}

void jsonRoundTrip()
{
    Preset p;
    p.name = "Intro";
    p.values["/a"] = "[0.5]";
    p.values["/b"] = "[\"x\",true]";
    p.actions = {{"/rewind", ""}, {"/cue", "[2,\"z\"]"}};
    const std::string json = p.toJson();
    CHECK_EQ(json, std::string("{\"actions\":[{\"address\":\"/rewind\"},{\"address\":\"/cue\",\"args\":[2,\"z\"]}],"
                               "\"name\":\"Intro\",\"values\":{\"/a\":[0.5],\"/b\":[\"x\",true]}}"));
    const Preset q = Preset::fromJson(json);
    CHECK_EQ(q.name, std::string("Intro"));
    CHECK(q.values == p.values);
    CHECK_EQ(q.actions.size(), std::size_t(2));
    CHECK(q.actions[0].address == "/rewind" && q.actions[0].args.empty() && q.actions[1].args == "[2,\"z\"]");
    CHECK_EQ(Preset().toJson(), std::string("{\"values\":{}}"));
}

void timingAndStepOverridesSurviveRecall()
{
    Show s;
    s.level.value = Value(0.0);
    Preset preset;
    preset.id = "34";
    preset.values["/level"] = "[0.8]";
    preset.fade = 4;
    preset.times["/level"] = 2;
    preset.delays["/level"] = 1;
    const auto loaded = Preset::fromJson(preset.toJson());
    CHECK_EQ(loaded.id, preset.id);
    CHECK_EQ(loaded.duration(), 3.0);

    Preset overrides;
    overrides.values["/level"] = "[1.0]";
    overrides.delays["/level"] = 0; // explicit zero overrides the preset
    PresetPlayback playback;
    const auto cue = loaded.resolved(overrides, 5);
    CHECK_EQ(cue.duration(), 2.0); // the parameter's own time still wins
    playback.start(s.tree, cue);
    playback.advance(s.tree, 0.5);
    CHECK(s.level.value == Value(0.25));
    playback.finish(s.tree);
    CHECK(s.level.value == Value(1.0));

    overrides.delays.clear();
    playback.start(s.tree, loaded.resolved(overrides));
    playback.advance(s.tree, 0.5);
    CHECK(s.level.value == Value(1.0));
}

void readingIsTolerant()
{
    for (const char *bad : {"", "not json", "[1,2]", "42", "null", "{\"values\":3}", "{\"values\":[1]}"}) {
        const Preset p = Preset::fromJson(bad);
        CHECK(p.values.empty() && p.actions.empty() && p.name.empty());
    }
    const Preset p = Preset::fromJson("{\"name\":7,\"values\":{\"/a\":[1],\"/b\":2},\"actions\":[3,{\"address\":5},{\"address\":\"\"},{\"address\":\"/ok\",\"args\":\"no\"},{}]}");
    CHECK(p.name.empty());
    CHECK_EQ(p.values.size(), std::size_t(2)); // a value that is no array is kept as it is, and read as no arguments when recalled
    CHECK_EQ(p.actions.size(), std::size_t(1));
    CHECK(p.actions[0].address == "/ok" && p.actions[0].args.empty());
}

void nestedArraysAreFlattened()
{
    Tree t;
    std::vector<std::string> seen;
    t.addAction("/m", "[ff]i", [&](const ValueList &a) {
        for (const Value &v : a) seen.push_back(v.text());
        return true;
    });
    Preset p;
    p.actions = {{"/m", "[[1.5,2.5],3,true,\"s\",null]"}};
    CHECK_EQ(recall(t, p).actions, 1);
    CHECK(seen == std::vector<std::string>({"1.5", "2.5", "3", "true", "s", ""})); // every number is a double; null is nothing
}

void presetsWithBadTextDoNotThrow()
{
    Preset p;
    p.name = "n\xFF";
    p.values["/a\xFE"] = "[\"x\xFF\"]"; // the text of a value is JSON, which has no invalid UTF-8: it is not read
    p.actions = {{"/b\xC3", "[\"\xFF\"]"}};
    const std::string json = p.toJson();
    CHECK(json.find("\xEF\xBF\xBD") != std::string::npos);
    (void)Preset::fromJson(json);
}

} // namespace

int main()
{
    captureHoldsWhatIsStored();
    recallPutsTheValuesBack();
    lockedAndMissingAreCounted();
    aValueThatDoesNotFitIsCountedAsMissing();
    jsonRoundTrip();
    timingAndStepOverridesSurviveRecall();
    readingIsTolerant();
    nestedArraysAreFlattened();
    presetsWithBadTextDoNotThrow();
    return pxs_test::finish("preset");
}
