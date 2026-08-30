/* The C that a libyaml binding cannot be written without.
 *
 * libyaml is a *pull* parser -- `yaml_parser_parse` answers one event at a time
 * and there is no callback anywhere -- so almost all of it declares directly and
 * this file is small. What is left is `15 §7`'s second bucket, a shape with no
 * sysl spelling:
 *
 *   - `yaml_event_t` carries its payload in an untagged **union**. Which arm is
 *     live is decided by the `type` field beside it, which is exactly what sysl
 *     has no way to say. `sysl_yaml_event` below is the same information as a
 *     flat record of scalars and pointers, and `sysl_yaml_reader_next` is what
 *     writes the live arm into one.
 *   - `yaml_parser_t` and `yaml_event_t` are both **caller-allocated**, and
 *     their sizes are the library's business. The sysl side places one
 *     `sysl_yaml_reader` in a `[N]u64` field, with `N` from a `c const` over
 *     `sizeof`, and never learns what is inside it.
 *
 * **The event is owned by the reader, not by the caller.** libyaml allocates the
 * scalar, the anchor and the tag, and `yaml_event_delete` frees them; so
 * `sysl_yaml_reader_next` deletes the previous event before parsing the next,
 * and `sysl_yaml_reader_done` deletes the one still in hand. That makes the
 * lifetime a property of this file rather than a rule the sysl side has to
 * remember: the pointers in a `sysl_yaml_event` are good until the next call.
 *
 * Nothing here is sysl-specific. It is the ordinary shim any language writes
 * when it binds a C library with a union in it, and the build compiles it
 * because it is sitting in the package's tree -- nothing lists it and nothing
 * declares it.
 */
#ifndef SYSL_YAML_SHIM_H
#define SYSL_YAML_SHIM_H

#include <yaml.h>
#include <stddef.h>

/* The parser, the event it last answered, and whether that event needs deleting.
 *
 * One struct rather than two so that the sysl side places one piece of storage
 * and the pairing of parse and delete cannot be got wrong from outside.
 */
typedef struct {
    yaml_parser_t parser;
    yaml_event_t event;
    int live;
} sysl_yaml_reader;

/* One event, with the union flattened.
 *
 * Every field is present for every event type; which ones mean anything is what
 * `type` says. `anchor`, `tag` and `value` are NULL where the event has none,
 * which is how the sysl side tells "no anchor" from "an empty anchor".
 */
typedef struct {
    int type;             /* yaml_event_type_t */
    int style;            /* scalar, sequence or mapping style; encoding for STREAM-START */
    int implicit;         /* document start/end, sequence/mapping tag, scalar plain_implicit */
    int quoted_implicit;  /* scalar only */
    const unsigned char *anchor;
    const unsigned char *tag;
    const unsigned char *value;
    size_t length;        /* of `value`, which may contain NUL bytes */
    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;
} sysl_yaml_event;

/* Nonzero on success. `input` must stay alive and unmoved until `done`:
 * libyaml reads from it rather than copying it.
 */
int sysl_yaml_reader_init(sysl_yaml_reader *r, const unsigned char *input, size_t length);

/* The next event, flattened into `out`. Nonzero on success; zero means the
 * parse failed and the `problem` accessors say why.
 */
int sysl_yaml_reader_next(sysl_yaml_reader *r, sysl_yaml_event *out);

/* Frees the event still in hand and then the parser. Safe to call twice. */
void sysl_yaml_reader_done(sysl_yaml_reader *r);

/* Which of libyaml's error classes the failure was: `yaml_error_type_t`. */
int sysl_yaml_reader_error(const sysl_yaml_reader *r);

/* libyaml's own sentence for the failure, or NULL. Static storage in libyaml. */
const char *sysl_yaml_reader_problem(const sysl_yaml_reader *r);

/* Where the failure was, zero-based. Meaningless unless `next` answered zero. */
size_t sysl_yaml_reader_problem_line(const sysl_yaml_reader *r);
size_t sysl_yaml_reader_problem_column(const sysl_yaml_reader *r);
size_t sysl_yaml_reader_problem_offset(const sysl_yaml_reader *r);

/* What was being parsed when it failed -- "while parsing a block mapping" and
 * the like -- or NULL, and where that started.
 */
const char *sysl_yaml_reader_context(const sysl_yaml_reader *r);
size_t sysl_yaml_reader_context_line(const sysl_yaml_reader *r);
size_t sysl_yaml_reader_context_column(const sysl_yaml_reader *r);

#endif
