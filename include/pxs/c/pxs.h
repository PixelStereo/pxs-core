/* pxs-core's C interface: what a program that is not C++ (a Flutter application through dart:ffi, a script, another language's runtime)
 * needs from pxs-core: the OSC codec, the address space (parameters, actions, signals, OSC applied to it, what an OSCQuery client is
 * told), presets, DMX frames, MIDI messages, the WebSocket handshake.
 *
 * Rules, the same for every function:
 *  - Nothing throws and nothing aborts: a failure is a status (a negative number or NULL, and `status` when there is one).
 *  - Nothing is thread safe: use one object from one thread at a time (an isolate's, a UI's); callbacks run on the thread that called.
 *  - What a function returns that you own (a buffer, a string) is freed with pxs_free; an object with pxs_<name>_free. What you hand
 *    to a function is copied before it returns: it need not outlive the call. A pointer inside a result is valid until the object that
 *    holds it is freed, or the next call that changes it (said where it matters).
 *  - Text is UTF-8, NUL-terminated; sizes are in bytes.
 *  - The version of this interface is PXS_C_API_VERSION: it grows by adding, and changes only with a new major version.
 */
#ifndef PXS_C_PXS_H
#define PXS_C_PXS_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(PXS_C_SHARED)
#if defined(PXS_C_BUILDING)
#define PXS_API __declspec(dllexport)
#else
#define PXS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define PXS_API __attribute__((visibility("default")))
#else
#define PXS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PXS_C_API_VERSION 1

/* ---- general ------------------------------------------------------------------------------------------------------------------ */

typedef enum {
    PXS_OK = 0,
    PXS_ERR_ARGUMENT = -1,  /* a NULL, a size or a number that cannot be */
    PXS_ERR_MALFORMED = -2, /* a packet or a text that is not what it should be, or beyond a limit */
    PXS_ERR_NOT_FOUND = -3, /* no node at that address */
    PXS_ERR_REFUSED = -4,   /* the node does not take that (locked, read only, a value that does not fit) */
    PXS_ERR_INTERNAL = -5   /* an exception was caught at the boundary: a bug, say it */
} pxs_status;

PXS_API int pxs_api_version(void);      /* PXS_C_API_VERSION of the library that is loaded */
PXS_API const char *pxs_version(void);  /* pxs-core's version, "0.9.0" (a static string) */
PXS_API void pxs_free(void *p);         /* frees a buffer or a string this library returned; NULL is fine */

/* ---- values ------------------------------------------------------------------------------------------------------------------- */

typedef enum {
    PXS_NIL = 0, PXS_BOOL, PXS_INT32, PXS_INT64, PXS_FLOAT, PXS_DOUBLE, PXS_STRING, PXS_VEC2, PXS_VEC3, PXS_VEC4, PXS_BLOB
} pxs_kind;

/* A value of the address space. Which members mean something depends on `kind`: BOOL boolean; INT32 and INT64 integer; FLOAT and DOUBLE
 * number; VEC2/3/4 vec[0..n-1]; STRING and BLOB data and size (a string's size does not count its NUL). A value you give is read
 * during the call; a value you get points into what holds it. */
typedef struct pxs_value {
    int32_t kind;
    int32_t boolean;
    int64_t integer;
    double number;
    float vec[4];
    const uint8_t *data;
    size_t size;
} pxs_value;

/* ---- OSC ---------------------------------------------------------------------------------------------------------------------- */

typedef struct pxs_osc_packet pxs_osc_packet; /* the messages of one decoded packet (a message, or a bundle flattened) */

/* NULL, and `*status` (when not NULL) says why, for a malformed, empty or oversized packet. */
PXS_API pxs_osc_packet *pxs_osc_decode(const uint8_t *data, size_t size, int *status);
PXS_API void pxs_osc_packet_free(pxs_osc_packet *packet);
PXS_API size_t pxs_osc_packet_count(const pxs_osc_packet *packet);
/* i-th message: its address and type tags (without the leading ',') are valid until the packet is freed; NULL when i is out of range. */
PXS_API const char *pxs_osc_message_address(const pxs_osc_packet *packet, size_t i);
PXS_API const char *pxs_osc_message_types(const pxs_osc_packet *packet, size_t i);
PXS_API size_t pxs_osc_message_argc(const pxs_osc_packet *packet, size_t i);
/* The k-th argument of the i-th message as a value (a number tag is INT32/INT64/FLOAT/DOUBLE, T and F BOOL, s S STRING, b BLOB,
 * N I and the rest NIL; c r m are INT64). PXS_OK, or PXS_ERR_ARGUMENT. */
PXS_API int pxs_osc_message_arg(const pxs_osc_packet *packet, size_t i, size_t k, pxs_value *out);

/* A message to encode: its address, its type tags (without the leading ','), one value per tag, in order. Each value is written as its
 * tag says (an 'i' takes a number and rounds it, an 'f' a float…); a missing one is the tag's zero. */
PXS_API int pxs_osc_encode(const char *address, const char *types, const pxs_value *args, size_t argc, uint8_t **out, size_t *size);

/* ---- the address space -------------------------------------------------------------------------------------------------------- */

typedef struct pxs_tree pxs_tree;

PXS_API pxs_tree *pxs_tree_new(void);
PXS_API void pxs_tree_free(pxs_tree *tree);

typedef enum { PXS_PARAM_FLOAT = 0, PXS_PARAM_INT, PXS_PARAM_BOOL, PXS_PARAM_CHOICE, PXS_PARAM_TEXT, PXS_PARAM_TRIGGER } pxs_param_type;

/* Where a parameter's value lives: `get` fills `*out` (a string it points to must stay valid until the next call of `get` or `set` on
 * the same parameter) and returns 0; `set` takes the value written (already held within the parameter's limits) and returns 0. */
typedef int (*pxs_get_fn)(void *user, pxs_value *out);
typedef int (*pxs_set_fn)(void *user, const pxs_value *in);
/* An action: the values it got, one per argument; returns 0 when it did it. */
typedef int (*pxs_action_fn)(void *user, const pxs_value *args, size_t argc);
/* Everything the tree says (the feedback of a parameter, a signal fired): an OSC packet, to send. */
typedef void (*pxs_output_fn)(void *user, const uint8_t *packet, size_t size);

/* A parameter at `path` ("/layer/1/opacity"). FLOAT and INT take min and max (the range shown and the limits); CHOICE takes its
 * keys (lower case) in `choices`; the others ignore both. `get` and `set` may be NULL for a parameter that is only written or only read. */
PXS_API int pxs_tree_add_parameter(pxs_tree *tree, const char *path, const char *label, int type, double min, double max, const char *const *choices,
                                   size_t choice_count, pxs_get_fn get, pxs_set_fn set, void *user);
/* A node declared by hand, of any arguments: a point ("ff"), a colour ("fff"), a text with its keys, with a range per argument, a default, and the
 * way to read and write its values. `get` fills up to `capacity` values and says how many in `*count` (a string a value points to stays valid
 * until the next call of `get` or `set` of this node); `set` takes the values written (one per argument) and returns 0 when it took them. A
 * node with a value may hold other nodes (add them at their own paths). */
typedef int (*pxs_values_get_fn)(void *user, pxs_value *out, size_t capacity, size_t *count);
typedef int (*pxs_values_set_fn)(void *user, const pxs_value *in, size_t count);
typedef struct pxs_node {
    const char *path;         /* "/kadran/controls/1/pad" */
    const char *types;        /* OSC type tags: "f", "ff", "fff", "s", "T"… */
    int32_t access;           /* 1 read, 2 write, 3 both */
    const char *description;  /* may be NULL */
    const char *unit;         /* OSCQuery UNIT of every argument ("distance.m"), or NULL */
    const double *min;        /* the range of each argument: `range_count` entries (one per argument); NULL: none */
    const double *max;
    size_t range_count;
    const char *const *vals;  /* the keys a text node takes (RANGE VALS), or NULL */
    size_t vals_count;
    const pxs_value *defaults; /* DEFAULT_VALUE, one per argument, or NULL: a reset writes these */
    size_t defaults_count;
    pxs_values_get_fn get;    /* NULL: not readable */
    pxs_values_set_fn set;    /* NULL: not writable */
    void *user;
} pxs_node;
PXS_API int pxs_tree_add_node(pxs_tree *tree, const pxs_node *node);
/* An action at `path` ("N": no argument), or a signal the application emits. `types` are OSC type tags. */
PXS_API int pxs_tree_add_action(pxs_tree *tree, const char *path, const char *types, pxs_action_fn action, void *user);
PXS_API int pxs_tree_add_signal(pxs_tree *tree, const char *path, const char *types);
PXS_API int pxs_tree_remove(pxs_tree *tree, const char *path); /* PXS_OK or PXS_ERR_NOT_FOUND */

PXS_API void pxs_tree_set_output(pxs_tree *tree, pxs_output_fn output, void *user); /* NULL: nothing is told */
/* Applies the messages of an OSC packet from the network; returns how many nodes took one, or a negative status. */
PXS_API int pxs_tree_apply_packet(pxs_tree *tree, const uint8_t *data, size_t size);
/* Says that a parameter now holds another value (its message goes to the output), or fires a signal with these values. */
PXS_API int pxs_tree_changed(pxs_tree *tree, const char *path);
PXS_API int pxs_tree_fire(pxs_tree *tree, const char *path, const pxs_value *args, size_t argc);
PXS_API int pxs_tree_lock(pxs_tree *tree, const char *path, int locked);

/* What an OSCQuery server answers to a request line (`method` "GET", `target` "/layer/1?VALUE"): the JSON body is returned (free it with
 * pxs_free; NULL when there is none) and `*status` is the HTTP status (200, 204, 400, 404, 405). `name` and `osc_port` are what HOST_INFO says. */
PXS_API char *pxs_oscquery_answer(const pxs_tree *tree, const char *name, int osc_port, const char *method, const char *target, int *status);

/* Presets: the values of every parameter that is saved, as JSON text (free it); and put back from such a text. Returns the number of values
 * written, or a negative status. */
PXS_API char *pxs_preset_capture(const pxs_tree *tree, const char *name);
PXS_API int pxs_preset_recall(pxs_tree *tree, const char *json);

/* ---- the other side: a device's tree, as a client reads it -------------------------------------------------------------------- */

/* What an OSCQuery device answers to GET /, kept as a tree to browse and read. The sockets are the caller's (HTTP, WebSocket LISTEN, UDP): this
 * says what the answer and the values mean, with the same rule as the server's: a node with a TYPE or an ACCESS has a value, whether it holds
 * other nodes or not; one with neither is only a container. A reset is writing the node's DEFAULT_VALUE. */
typedef struct pxs_remote pxs_remote;

PXS_API pxs_remote *pxs_remote_new(void);
PXS_API void pxs_remote_free(pxs_remote *remote);
/* Replaces everything with an answer to GET / (JSON text). PXS_OK, or PXS_ERR_MALFORMED when it is not a node. */
PXS_API int pxs_remote_load(pxs_remote *remote, const char *json);
/* Forgets everything (the device went away). */
PXS_API void pxs_remote_clear(pxs_remote *remote);
/* A value the device told (LISTEN, ?VALUE): `value_json` is the JSON array, one entry per argument. PXS_OK, PXS_ERR_NOT_FOUND or PXS_ERR_MALFORMED. */
PXS_API int pxs_remote_set_value(pxs_remote *remote, const char *path, const char *value_json);
/* One node as JSON (free it): path, name, type (OSC type tags), access, kind ("action", "signal" or ""), description, hasValue, container,
 * children (their names), value and default (arrays), and for a node of one argument "param": type (float, int, bool, choice, text, trigger),
 * label, access, min, max, lo, hi, step, unbounded, signal, action, unit, extendedType, choices, default. NULL when there is no such node. */
PXS_API char *pxs_remote_node(const pxs_remote *remote, const char *path);
/* What the tree is made of, not its values (free it): two trees with the same signature show the same. */
PXS_API char *pxs_remote_signature(const pxs_remote *remote);
/* The addresses of every node that has a value, in order, as a JSON array (free it). */
PXS_API char *pxs_remote_leaves(const pxs_remote *remote);

/* ---- DMX over the network (Art-Net, sACN) ------------------------------------------------------------------------------------- */

typedef struct pxs_dmx_frame {
    int32_t sacn;        /* 1: sACN, 0: Art-Net */
    int32_t universe;
    int32_t priority;    /* sACN */
    int32_t sequence;
    int32_t length;      /* channels in the packet (1..512) */
    int32_t terminated;  /* sACN */
    uint8_t cid[16];     /* sACN: the sender's id */
    char name[64];       /* sACN: the source's name, NUL-terminated */
    uint8_t values[512]; /* the levels */
} pxs_dmx_frame;

/* The packet of a frame (free it); PXS_ERR_ARGUMENT when the universe or the priority is out of range. */
PXS_API int pxs_dmx_encode(const pxs_dmx_frame *frame, uint8_t **out, size_t *size);
/* `sacn` says which protocol to expect. PXS_ERR_MALFORMED for anything that is not DMX of that protocol. */
PXS_API int pxs_dmx_decode(const uint8_t *data, size_t size, int sacn, pxs_dmx_frame *frame);

/* ---- MIDI channel messages ---------------------------------------------------------------------------------------------------- */

typedef struct pxs_midi_message {
    int32_t kind;    /* 0 control change, 1 note, 2 pitch bend, 3 program change */
    int32_t channel; /* 1..16 */
    int32_t number;  /* the controller, the note, the program */
    double value;    /* 0..1 (a note off is 0) */
} pxs_midi_message;

PXS_API int pxs_midi_parse(const uint8_t *data, size_t size, pxs_midi_message *out); /* PXS_OK, or PXS_ERR_MALFORMED (not a channel message we read) */
/* The bytes of a message: `kind` 0 control change, 1 note on, 2 note off, 3 pitch bend, 4 program change. Free the buffer. */
PXS_API int pxs_midi_build(int kind, int channel, int number, double value, uint8_t **out, size_t *size);

/* ---- WebSocket ---------------------------------------------------------------------------------------------------------------- */

/* The Sec-WebSocket-Accept of a handshake, from the client's Sec-WebSocket-Key (free the string). */
PXS_API char *pxs_websocket_accept_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* PXS_C_PXS_H */
