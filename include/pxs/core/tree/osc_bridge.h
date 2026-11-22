#pragma once
// The two vocabularies of a value: the address space's (`Value`) and the wire's (`osc::Value`, one alternative per OSC type
// tag). The tag decides the type on the wire, as it does in an OSC message: an argument is written as what its tag says.
#include <pxs/core/osc/codec.h>
#include <pxs/core/value.h>

namespace pxs::core::tree {

// What an OSC argument is in the address space: i -> int32; h, t -> int64; f -> float; d -> double; s, S -> string; b -> blob;
// c, r, m -> int64 (the unsigned 32 bits as a number); T, F -> bool; N, I and the unknown -> Nil.
Value fromOsc(const osc::Value &argument);

// A value as the argument of a tag: a number tag takes the value read as a number (0 when it is none, a whole number
// saturating at the limits of its type), a string tag its text, a blob tag its bytes, T and F nothing.
osc::Value toOsc(char tag, const Value &value);

// The arguments of a message from the values of a node, one per scalar tag of `type` (the brackets of an array are no
// argument); a Vec2/Vec3/Vec4 among the values is as many floats, in order.
std::vector<osc::Value> toOscArguments(const std::string &type, const ValueList &values);

} // namespace pxs::core::tree
