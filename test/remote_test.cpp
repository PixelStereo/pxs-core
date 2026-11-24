// The client side of the tree: what a device answers to GET / read as a tree, with the server's own rule for a node with a value.
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/remote.h>
#include <pxs/core/tree/tree.h>

#include "check.h"

using namespace pxs::core;
using namespace pxs::core::tree;

namespace {

const char *kDevice = R"({
  "FULL_PATH": "/", "ACCESS": 0,
  "CONTENTS": {
    "color": {"FULL_PATH": "/color", "ACCESS": 0, "CONTENTS": {
      "tint": {"FULL_PATH": "/color/tint", "TYPE": "f", "ACCESS": 3, "VALUE": [0.25], "DEFAULT_VALUE": [0.0],
               "RANGE": [{"MIN": -100, "MAX": 100}], "CONTENTS": {
        "enable": {"FULL_PATH": "/color/tint/enable", "TYPE": "T", "ACCESS": 3, "VALUE": [true]}}},
      "add": {"FULL_PATH": "/color/add", "TYPE": "fff", "ACCESS": 3, "VALUE": [0.1, 0.2, 0.3],
              "RANGE": [{"MIN": 0, "MAX": 1}, {"MIN": 0, "MAX": 1}, {"MIN": 0, "MAX": 1}], "CONTENTS": {
        "r": {"FULL_PATH": "/color/add/r", "TYPE": "f", "ACCESS": 3, "VALUE": [0.1]}}}}},
    "blend": {"FULL_PATH": "/blend", "TYPE": "s", "ACCESS": 3, "VALUE": ["add"], "RANGE": [{"VALS": ["normal", "add"]}]},
    "go": {"FULL_PATH": "/go", "TYPE": "N", "ACCESS": 2},
    "peak": {"FULL_PATH": "/peak", "TYPE": "f", "ACCESS": 1}
  }})";

void aValueThatHoldsNodesIsAParameter()
{
    RemoteTree t;
    CHECK(t.load(kDevice) && t.loaded());
    const RemoteNode *tint = t.find("/color/tint");
    CHECK(tint && tint->hasValue() && !tint->isContainer() && tint->children.size() == 1);
    CHECK(t.find("/color")->isContainer());
    ParamInfo info;
    CHECK(tint->toParamInfo(&info) && info.type == ParamInfo::Type::Float && info.min == -100 && info.max == 100);
    CHECK(info.defaultValue.number().value_or(9) == 0.0); // announced, not guessed from VALUE
    CHECK(tint->value.size() == 1 && tint->value[0].number().value_or(9) == 0.25);
    CHECK(t.find("/color/tint/enable")->hasValue());
    const std::vector<std::string> leaves = t.leaves();
    CHECK(std::find(leaves.begin(), leaves.end(), "/color/tint") != leaves.end());
    CHECK(std::find(leaves.begin(), leaves.end(), "/color") == leaves.end());
}

void whatIsNotAParameter()
{
    RemoteTree t;
    t.load(kDevice);
    ParamInfo info;
    CHECK(!t.find("/color/add")->toParamInfo(&info)); // three arguments: not one parameter (its r, g, b are)
    CHECK(t.find("/color/add")->hasValue() && t.find("/color/add")->range.size() == 3);
    CHECK(t.find("/blend")->toParamInfo(&info) && info.type == ParamInfo::Type::Choice && info.choices.size() == 2);
    CHECK(t.find("/go")->kind == "action" && t.find("/go")->toParamInfo(&info) && info.type == ParamInfo::Type::Trigger);
    CHECK(t.find("/peak")->kind == "signal" && t.find("/peak")->toParamInfo(&info) && info.signal);
    CHECK(!t.find("/color/tint")->toParamInfo(&info) == false);
    info = ParamInfo();
    t.find("/blend")->toParamInfo(&info);
    CHECK(info.defaultValue.isNil()); // no DEFAULT_VALUE: nothing to reset to
}

void valuesAreFollowed()
{
    RemoteTree t;
    t.load(kDevice);
    const std::string before = t.signature();
    CHECK(t.setValueJson("/color/tint", "[0.75]") && t.find("/color/tint")->value[0].number().value_or(9) == 0.75);
    CHECK(t.setValueJson("/color/add", "[0.5, 0.6, 0.7]") && t.find("/color/add")->value.size() == 3);
    CHECK(!t.setValueJson("/nope", "[1]") && !t.setValueJson("/color/tint", "not json"));
    CHECK_EQ(t.signature(), before); // the signature is of what the tree is made of, not of its values
}

void whatAServerSaysIsWhatAClientReads()
{
    Tree server;
    auto level = std::make_unique<Parameter>("level", "Level", Parameter::Type::Float);
    double v = 0.4;
    level->range(0, 2).byDefault(Value(1.0)).bind([&] { return Value(v); }, [&](const Value &x) { v = x.number().value_or(0.0); });
    server.add("/mix/level", std::move(level));
    server.add("/mix/level/mute").type = "T"; // a value that holds a node
    server.find("/mix/level/mute")->access = 3;
    RemoteTree client;
    CHECK(client.load(describeJson(server)));
    const RemoteNode *n = client.find("/mix/level");
    ParamInfo info;
    CHECK(n && n->hasValue() && n->children.count("mute") == 1);
    CHECK(n->toParamInfo(&info) && info.max == 2 && info.defaultValue.number().value_or(9) == 1.0);
    CHECK(n->value.size() == 1 && n->value[0].number().value_or(9) == 0.4);
    const std::string json = remoteNodeJson(client, "/mix/level");
    CHECK(json.find("\"hasValue\":true") != std::string::npos && json.find("\"param\"") != std::string::npos);
    CHECK_EQ(remoteNodeJson(client, "/nothing"), std::string("{}"));
}

} // namespace

int main()
{
    aValueThatHoldsNodesIsAParameter();
    whatIsNotAParameter();
    valuesAreFollowed();
    whatAServerSaysIsWhatAClientReads();
    return pxs_test::finish("remote");
}
