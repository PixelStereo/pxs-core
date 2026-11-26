#pragma once
// A preset by address: the values of some parameters of a Tree, and some actions to do after them. What an application keeps as a
// snapshot, a cue or a scene is this, whatever its parameters are: the tree says which ones there are.
//
// A parameter is held by its value (the VALUE OSCQuery would say). An action is held as "do this when the preset is recalled",
// with its arguments if it has some (Rewind, Start, Go to a cue): an action has no state, so a preset that holds one is what makes
// it part of a snapshot. Which actions a preset holds is its own choice: recalling a preset does not do every action of the
// application, only the ones it was given.
//
// Recalling puts the values first, then does the actions in their order, as a person would: the media is set to play, then
// rewound. A locked parameter keeps its value; an address the tree no longer has is counted, not an error.
#include <pxs/core/tree/tree.h>

#include <map>
#include <string>
#include <vector>

namespace pxs::core::tree {

struct Preset {
    struct Action {
        std::string address;
        std::string args; // a JSON array as OSCQuery writes values: numbers, strings, booleans (nested for an array); "" or "[]": none
    };

    std::string name;
    std::string scope; // selected capture scope in the preset library
    std::map<std::string, std::string> timelines; // stable timeline id -> JSON definition, restored by the application's timeline host
    std::map<std::string, std::string> values; // address -> its VALUE, a JSON array (one entry per argument)
    std::vector<Action> actions;               // done after the values, in this order

    double fade = 0;
    std::map<std::string, double> times, delays;
    std::map<std::string, int> curves;
    double timeAt(const std::string &path, double fadeOverride = -1) const;
    double delayAt(const std::string &path) const;
    int curveAt(const std::string &path) const;
    std::string id;
    Preset resolved(const Preset &overrides, double fadeOverride = -1) const;
    double duration() const;

    bool isEmpty() const { return values.empty() && actions.empty() && timelines.empty(); }
    // The preset as JSON text, and back (a text that is not a JSON object, or members of another type, give what could be read)
    std::string toJson() const;
    static Preset fromJson(const std::string &json);
};

// What a recall did
struct RecallResult {
    int applied = 0; // values written
    int actions = 0; // actions done
    int locked = 0;  // values and actions refused: the node is locked
    int missing = 0; // addresses the tree does not have (or that cannot be written)
};

// Holds the parameters at these addresses (none: every parameter that is saved, see Parameter::isStored, and every node filled in
// by hand that is read and written), and these actions
Preset capture(const Tree &tree, const std::vector<std::string> &only = {}, const std::vector<Preset::Action> &actions = {});
RecallResult recall(Tree &tree, const Preset &preset);

// The owner advances this with its show clock. A newer recall replaces only overlapping addresses.
class PresetPlayback {
public:
    void start(Tree &tree, const Preset &preset);
    void advance(Tree &tree, double seconds);
    void finish(Tree &tree);
    bool active() const { return !m_jobs.empty(); }
    void clear() { m_jobs.clear(); }

private:
    struct Job {
        std::string from, to;
        double delay, time;
        int curve;
        double elapsed = 0;
    };
    std::map<std::string, Job> m_jobs;
};

} // namespace pxs::core::tree
