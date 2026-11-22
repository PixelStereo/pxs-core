#include <pxs/core/tree/oscquery.h>

#include "check.h"

#include <cmath>

using namespace pxs::core;
using namespace pxs::core::tree;
using Type = ParamInfo::Type;

namespace {

struct Cell {
    Value value;
};

std::unique_ptr<Parameter> make(Cell &cell, const std::string &path, Type type)
{
    auto p = std::make_unique<Parameter>(path, "Cat \xE2\x80\xBA " + path, type);
    p->bind([&cell] { return cell.value; }, [&cell](const Value &v) { cell.value = v; });
    return p;
}

void aParameterIsDescribed()
{
    Tree t;
    Cell c;
    c.value = Value(0.5);
    auto p = make(c, "level", Type::Float);
    p->range(0, 2).byDefault(Value(1.0)).unit("gain.db");
    t.add("/mix/level", std::move(p));
    CHECK_EQ(describeJson(t, "/mix/level"),
             std::string("{\"ACCESS\":3,\"CLIPMODE\":[\"both\"],\"DEFAULT_VALUE\":[1.0],\"FULL_PATH\":\"/mix/level\","
                         "\"RANGE\":[{\"MAX\":2.0,\"MIN\":0.0}],\"TYPE\":\"f\",\"UNIT\":[\"gain.db\"],\"VALUE\":[0.5]}"));
}

void aContainerHasItsContents()
{
    Tree t;
    Cell c;
    c.value = Value(true);
    t.add("/a/on", make(c, "on", Type::Bool));
    t.add("/a/empty");
    CHECK_EQ(describeJson(t, "/a"),
             std::string("{\"ACCESS\":0,\"CONTENTS\":{\"empty\":{\"ACCESS\":0,\"CONTENTS\":{},\"FULL_PATH\":\"/a/empty\"},"
                         "\"on\":{\"ACCESS\":3,\"FULL_PATH\":\"/a/on\",\"TYPE\":\"T\",\"VALUE\":[true]}},\"FULL_PATH\":\"/a\"}"));
    CHECK_EQ(describeJson(t, "/nowhere"), std::string("{}"));
    CHECK(describeJson(t).find("\"FULL_PATH\":\"/\"") != std::string::npos);
}

void choicesAreListed()
{
    Tree t;
    Cell c;
    c.value = Value("b");
    auto p = make(c, "mode", Type::Choice);
    p->choices({"a", "b"});
    t.add("/mode", std::move(p));
    const std::string json = describeJson(t, "/mode");
    CHECK(json.find("\"RANGE\":[{\"VALS\":[\"a\",\"b\"]}]") != std::string::npos);
    CHECK(json.find("\"VALUE\":[\"b\"]") != std::string::npos);
}

void typeTagsAreWrittenAsOscQueryDoes()
{
    CHECK_EQ(valueJson('i', Value(2.5)), std::string("3"));
    CHECK_EQ(valueJson('i', Value("12")), std::string("12"));
    CHECK_EQ(valueJson('h', Value(std::int64_t(1) << 40)), std::string("1099511627776"));
    CHECK_EQ(valueJson('s', Value("h\xC3\xA9")), std::string("\"h\xC3\xA9\""));
    CHECK_EQ(valueJson('T', Value(0)), std::string("false"));
    CHECK_EQ(valueJson('F', Value(1)), std::string("true"));
    CHECK_EQ(valueJson('r', Value(std::int64_t(0xFF8040C0LL))), std::string("\"#FF8040C0\""));
    CHECK_EQ(valueJson('r', Value(std::int64_t(0x1))), std::string("\"#00000001\""));
    CHECK_EQ(valueJson('N', Value(1)), std::string("null"));
    CHECK_EQ(valueJson('b', Value(Blob{1})), std::string("null"));
    CHECK_EQ(valueJson('f', Value(0.25)), std::string("0.25"));
    CHECK_EQ(valueJson('f', Value(std::nan(""))), std::string("0.0")); // JSON has no not-a-number
    CHECK_EQ(valueJson('f', Value()), std::string("0.0"));
    CHECK_EQ(valuesJson("[ff]i", {Value(1.0), Value(2.0), Value(3)}), std::string("[[1.0,2.0],3]"));
    CHECK_EQ(valuesJson("ff", {Value(1.0), Value(2.0)}), std::string("[1.0,2.0]"));
    CHECK_EQ(valuesJson("fs", {Value(1.5)}), std::string("[1.5,\"\"]")); // a missing value is the tag's zero
}

void attributesAreAskedByName()
{
    Tree t;
    Cell c;
    c.value = Value(1.0);
    Node &n = t.add("/x", make(c, "x", Type::Float));
    n.attributes["PXS_NOTE"] = [] { return std::optional<std::string>("\"hello\""); };
    n.attributes["PXS_NONE"] = [] { return std::optional<std::string>(); };
    n.attributes["PXS_NULL"] = [] { return std::optional<std::string>("null"); };
    n.attributes["PXS_BROKEN"] = [] { return std::optional<std::string>("{nope"); };
    n.attributes["PXS_OBJ"] = [] { return std::optional<std::string>("{\"b\":1,\"a\":[true]}"); };
    std::string out;
    CHECK(attributeJson(t, "/x", "value", out) && out == "{\"VALUE\":[1.0]}"); // the name is matched in upper case
    CHECK(attributeJson(t, "/x", "TYPE", out) && out == "{\"TYPE\":\"f\"}");
    CHECK(attributeJson(t, "/x", "pxs_note", out) && out == "{\"PXS_NOTE\":\"hello\"}");
    CHECK(attributeJson(t, "/x", "PXS_OBJ", out) && out == "{\"PXS_OBJ\":{\"a\":[true],\"b\":1}}");
    for (const char *none : {"PXS_NONE", "PXS_NULL", "PXS_BROKEN", "NOPE", "CONTENTS"}) CHECK(attributeJson(t, "/x", none, out) && out == "{}");
    CHECK(!attributeJson(t, "/nowhere", "VALUE", out));
    const std::string whole = describeJson(t, "/x");
    CHECK(whole.find("PXS_NOTE") != std::string::npos && whole.find("PXS_NONE") == std::string::npos && whole.find("PXS_BROKEN") == std::string::npos);
    Tree u;
    u.add("/c/d");
    CHECK(attributeJson(u, "/c", "contents", out) && out.find("\"CONTENTS\":{\"d\"") != std::string::npos);
}

void lockedAndTagsAndExtendedTypes()
{
    Tree t;
    Cell c;
    Node &n = t.add("/color/g", make(c, "g", Type::Float));
    n.tags = {"x", "y"};
    t.setLocked("/color/g", true);
    const std::string json = describeJson(t, "/color/g");
    CHECK(json.find("\"PXS_LOCKED\":true") != std::string::npos);
    CHECK(json.find("\"TAGS\":[\"x\",\"y\"]") != std::string::npos);
    Tree u;
    Cell d;
    auto p = make(d, "g", Type::Float);
    p->colorComponent(1);
    u.add("/g", std::move(p));
    CHECK(describeJson(u, "/g").find("\"EXTENDED_TYPE\":[\"color.rgb.g\"]") != std::string::npos);
}

void hostInfoSaysWhatIsSupported()
{
    CHECK_EQ(hostInfoJson("App", 9000, "{\"VALUE\":true}", "{}"),
             std::string("{\"EXTENSIONS\":{\"VALUE\":true},\"NAME\":\"App\",\"OSC_PORT\":9000,\"OSC_TRANSPORT\":\"UDP\"}"));
    CHECK_EQ(hostInfoJson("App", 9000, "{\"VALUE\":true}", "{\"NAME\":\"Other\",\"X\":1}"),
             std::string("{\"EXTENSIONS\":{\"VALUE\":true},\"NAME\":\"Other\",\"OSC_PORT\":9000,\"OSC_TRANSPORT\":\"UDP\",\"X\":1}")); // `more` wins
    CHECK(hostInfoJson("App", 1, "not json", "[1]").find("\"EXTENSIONS\":{}") != std::string::npos);
    const std::string ext = defaultExtensionsJson();
    for (const char *key : {"ACCESS", "VALUE", "RANGE", "DESCRIPTION", "CLIPMODE", "PXS_LOCKED", "TAGS", "EXTENDED_TYPE", "UNIT", "DEFAULT_VALUE"})
        CHECK(ext.find(std::string("\"") + key + "\":true") != std::string::npos);
}

void textThatIsNotUtf8DoesNotThrow()
{
    // Found by the fuzzer: the JSON library throws when it dumps a string that is not valid UTF-8, and an OSC string from the network
    // may be anything. It comes out with U+FFFD in its place.
    Tree t;
    Cell c;
    c.value = Value("a\xFF" "b");
    Node &n = t.add("/s", make(c, "s", Type::Text));
    n.description = "d\xC3";
    const std::string json = describeJson(t, "/s");
    CHECK(json.find("\"VALUE\":[\"a\xEF\xBF\xBD" "b\"]") != std::string::npos);
    CHECK(json.find("\"DESCRIPTION\":\"d\xEF\xBF\xBD\"") != std::string::npos);
    CHECK(hostInfoJson("n\xFF", 1).find("n\xEF\xBF\xBD") != std::string::npos);
    CHECK_EQ(valueJson('s', Value("\xC0\xAF")), std::string("\"\xEF\xBF\xBD\xEF\xBF\xBD\""));
}

} // namespace

int main()
{
    aParameterIsDescribed();
    aContainerHasItsContents();
    choicesAreListed();
    typeTagsAreWrittenAsOscQueryDoes();
    attributesAreAskedByName();
    lockedAndTagsAndExtendedTypes();
    hostInfoSaysWhatIsSupported();
    textThatIsNotUtf8DoesNotThrow();
    return pxs_test::finish("oscquery");
}
