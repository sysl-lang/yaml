#include "shim.h"

#include <string.h>

int sysl_yaml_reader_init(sysl_yaml_reader *r, const unsigned char *input, size_t length) {
    memset(r, 0, sizeof(*r));

    if (!yaml_parser_initialize(&r->parser))
        return 0;

    /* **An empty document arrives as a null pointer**, because `sysl.slices.as_ptr` of an empty
     * slice is null -- there is no first element to take the address of. libyaml asserts that its
     * input is not null, so an empty file would abort the process rather than parse to an empty
     * stream. A pointer to nothing of length zero is what it means.
     */
    yaml_parser_set_input_string(&r->parser, input ? input : (const unsigned char *)"", length);
    return 1;
}

/* The whole of the union flattening.
 *
 * A `switch` rather than a chain of `if`s so that a libyaml which grew an event
 * type would fail to compile here with `-Wswitch` rather than silently answer a
 * record of zeroes.
 */
static void flatten(const yaml_event_t *e, sysl_yaml_event *out) {
    memset(out, 0, sizeof(*out));

    out->type = (int)e->type;
    out->start_line = e->start_mark.line;
    out->start_column = e->start_mark.column;
    out->end_line = e->end_mark.line;
    out->end_column = e->end_mark.column;

    switch (e->type) {
    case YAML_STREAM_START_EVENT:
        out->style = (int)e->data.stream_start.encoding;
        break;

    case YAML_DOCUMENT_START_EVENT:
        out->implicit = e->data.document_start.implicit;
        break;

    case YAML_DOCUMENT_END_EVENT:
        out->implicit = e->data.document_end.implicit;
        break;

    case YAML_ALIAS_EVENT:
        out->anchor = e->data.alias.anchor;
        break;

    case YAML_SCALAR_EVENT:
        out->anchor = e->data.scalar.anchor;
        out->tag = e->data.scalar.tag;
        out->value = e->data.scalar.value;
        out->length = e->data.scalar.length;
        out->implicit = e->data.scalar.plain_implicit;
        out->quoted_implicit = e->data.scalar.quoted_implicit;
        out->style = (int)e->data.scalar.style;
        break;

    case YAML_SEQUENCE_START_EVENT:
        out->anchor = e->data.sequence_start.anchor;
        out->tag = e->data.sequence_start.tag;
        out->implicit = e->data.sequence_start.implicit;
        out->style = (int)e->data.sequence_start.style;
        break;

    case YAML_MAPPING_START_EVENT:
        out->anchor = e->data.mapping_start.anchor;
        out->tag = e->data.mapping_start.tag;
        out->implicit = e->data.mapping_start.implicit;
        out->style = (int)e->data.mapping_start.style;
        break;

    case YAML_NO_EVENT:
    case YAML_STREAM_END_EVENT:
    case YAML_SEQUENCE_END_EVENT:
    case YAML_MAPPING_END_EVENT:
        break;
    }
}

int sysl_yaml_reader_next(sysl_yaml_reader *r, sysl_yaml_event *out) {
    if (r->live) {
        yaml_event_delete(&r->event);
        r->live = 0;
    }

    if (!yaml_parser_parse(&r->parser, &r->event))
        return 0;

    r->live = 1;
    flatten(&r->event, out);
    return 1;
}

void sysl_yaml_reader_done(sysl_yaml_reader *r) {
    if (r->live) {
        yaml_event_delete(&r->event);
        r->live = 0;
    }

    yaml_parser_delete(&r->parser);

    /* `yaml_parser_delete` leaves the struct zeroed, so a second call is a
     * second delete of an already-empty parser, which libyaml tolerates. The
     * flag above is what makes the event half safe to repeat.
     */
}

int sysl_yaml_reader_error(const sysl_yaml_reader *r) { return (int)r->parser.error; }

const char *sysl_yaml_reader_problem(const sysl_yaml_reader *r) { return r->parser.problem; }

size_t sysl_yaml_reader_problem_line(const sysl_yaml_reader *r) { return r->parser.problem_mark.line; }

size_t sysl_yaml_reader_problem_column(const sysl_yaml_reader *r) { return r->parser.problem_mark.column; }

size_t sysl_yaml_reader_problem_offset(const sysl_yaml_reader *r) { return r->parser.problem_offset; }

const char *sysl_yaml_reader_context(const sysl_yaml_reader *r) { return r->parser.context; }

size_t sysl_yaml_reader_context_line(const sysl_yaml_reader *r) { return r->parser.context_mark.line; }

size_t sysl_yaml_reader_context_column(const sysl_yaml_reader *r) { return r->parser.context_mark.column; }
