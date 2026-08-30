# yaml

YAML 1.2, read into a sysl value, bound to [libyaml](https://github.com/yaml/libyaml).

```sysl
import sh.sysl.yaml.{parse, describe}

parse(text) match
    Ok(doc) -> print(doc.get("kind").unwrap().as_str().unwrap())
    Err(e) -> print(describe(e))
```

```
dependencies {
  yaml { git = "github.com/sysl-lang/yaml", version = "0.1.0" }
}
```

The coordinate names the package, `yaml`; the module a program imports is `sh.sysl.yaml`.

## Installing libyaml, and the one flag this package needs today

libyaml is not vendored — it is a hosted C library every platform packages:

```
brew install libyaml
apt install libyaml-dev
pacman -S libyaml
```

**Building against it needs two flags for the moment**, and it is a compiler restriction rather than
anything about this package. A manifest declares an installed library under the name `pkg-config`
files it as, and libyaml files as **`yaml-0.1`** — a name sysl refuses today, because a `pkg_config`
key must also be usable as a `--include-path` name and that check does not allow a dot. So the
manifest declares a `headers` requirement instead, and a build says where libyaml is:

```
sysl test . --include-path yaml=$(pkg-config --variable=includedir yaml-0.1) \
            --link-path $(pkg-config --variable=libdir yaml-0.1)
```

That is filed as card `0380`. When it lands, the manifest becomes an ordinary `pkg_config` block, the
flags go away, and this package re-tags.

## What it is

A reader. There is no emitter: `parse` takes a document and answers with a `Value`, or with the first
thing wrong with it. Writing YAML properly — block style, chosen quoting, folding, a line width — is
a second package's worth of decisions and nothing here needs it yet.

The tree owns itself: a mapping is a `Buf[Entry]` and a sequence a `Buf[Value]`, with no reference
anywhere, so every walk over a document is an ordinary `match` and nothing has to be dereferenced
first. That is `sysl-lang/json`'s shape and `sysl-lang/toml`'s, deliberately — a program handed a
configuration file should not have to learn a third idiom because it arrived in a third format.

```sysl
enum Value
    Null
    Bool(b: bool)
    Int(n: long)
    Real(x: real)
    Str(s: string)
    Seq(items: Buf[Value])
    Map(entries: Buf[Entry])
```

**A mapping's key is a `Value`, not a `string`**, which is where this differs from JSON: `? [a, b]`
is a legal key and so is `1`. `get` takes a `string` because that is what a caller almost always has;
`entry(i)` is how the others are reached, and how a mapping is walked in the order it was written.

## Three decisions, each of which another YAML reader makes differently

**A plain scalar resolves by the YAML 1.2 core schema.** `true` and `false` are booleans; **`yes`,
`no`, `on` and `off` are strings**. A 1.1 tool disagrees, and this is the difference most likely to
be mistaken for a bug — it is also the safer answer, since a document meaning the boolean can write
`true`, while the 1.1 rule leaves no way to write the country code `NO`. Quoting always wins:
`"null"` is the four-letter string, and a literal block is text whatever it holds. An explicit
`!!int`, `!!float`, `!!bool`, `!!str` or `!!null` beats the schema; a tag this package does not know
leaves the value as its text.

**An alias is resolved into the tree, under a budget.** `*name` reads as the value its `&name`
introduced, so a document comes back as a tree rather than a graph and every walk is a plain `match`.
The value is *shared* rather than copied, so the memory is the document's — but the budget is charged
the expanded size, which is what refuses the "billion laughs" shape:

```sysl
parse_with(text, Limits(1000000, 1000))    // nodes, depth — the defaults
```

A document expanding past `max_nodes` is `TooLarge` and one nesting past `max_depth` is `TooDeep`,
both before anything runs for a noticeable time. **A recursive document is refused**: an anchor is
bound after the node it names has been read, so `&a [*a]` is `UnknownAlias` rather than a value that
contains itself. The **event** tier below hands over `Alias` unresolved, for a program that wants the
graph.

**A `<<` merge key is an ordinary key.** It is a YAML 1.1 extension that 1.2 dropped, and folding it
would make a key the caller can see disappear into the mapping around it. `get("<<")` answers
whatever the document wrote there.

## The event tier

`Parser` is libyaml's pull parser, one event at a time — which is what the document model is written
in terms of, and what a program streaming something large, or one that cares about anchors and
styles as written, wants instead.

```sysl
val p = parser(text)?

loop
    val l = p.next()?

    l.event match
        StreamEnd -> break
        Scalar(value, anchor, tag, style, _, _) -> print(l.start.line, value)
        _ -> ()
```

Every event carries the span it came from, and a scalar carries its anchor, its tag and the style it
was written in. Nothing has been resolved at this tier: `Alias` is a name, and a scalar is its text.

## Several documents in one stream

`parse` reads the one document a stream carries. An empty stream is `Null`; a stream carrying two is
`NotOneDocument`, rather than the first of them, so a program expecting one configuration file hears
about the second `---`. `parse_all` is what reads a Kubernetes manifest:

```sysl
for doc in parse_all(text)?.view()
    print(doc.get("kind").unwrap())
```

An anchor does not cross a `---`, which is what the spec says.

## What it is checked against

The [YAML test suite](https://github.com/yaml/yaml-test-suite) — 402 leaf cases from the `data`
branch, each an input document and either the event stream it must produce or a marker saying it must
be refused. The corpus is committed, so `sysl test .` runs it with no network.

**330 conform and 72 diverge.** The divergences are libyaml's rather than this binding's: libyaml is
a YAML **1.1** parser and the suite is written for **1.2**, so 51 valid documents are refused, 16
invalid ones are accepted, and 5 produce a different event stream. Every one is named in
`suite_tests.sysl` with what it does instead, and **the list is asserted in both directions** — a
case that starts conforming reddens the test and names the line to delete, so an upgrade of libyaml
that fixes one is reported rather than absorbed.

What that file checks is not "does libyaml agree with the suite", which is a fact about libyaml. It
is that *this binding reproduces libyaml exactly*, re-implementing the suite's own event notation
over `Parser` — which is what exercises every field of every event.

Beside it are 35 tests of everything the corpus cannot see: the schema, the anchors, the budgets, the
destructor, and the layout the C and the sysl have to agree about.

## The C

One file, `sh/sysl/yaml/c/shim.c`, and it is there for one reason: `yaml_event_t` carries its payload
in an untagged **union**, which sysl has no way to spell. The shim flattens the live arm into a
record of scalars and pointers and owns the event's lifetime — it deletes the previous event before
parsing the next — so the pointers a caller sees are good until the next call and cannot be got
wrong. Everything else in libyaml is declared directly.

The parser's C state is placed in a `[N]u64` field of the `&Parser` box, with `N` from a `c const`
over `sizeof`: libyaml's parser holds pointers into buffers it allocated, so it must not move, and a
box does not. `sizeof` and `_Alignof` are both asserted against the C in the tests rather than
trusted.

**Run under AddressSanitizer with `SYSL_EXTRA_CFLAGS="-fsanitize=address -g"`.** It instruments the
sysl and the shim; libyaml itself was compiled by whoever packaged it, so it covers this package's
half and not the library's. A leak is a measurement rather than a test: 50,000 and 500,000 parses of
the same document hold 2.21 MB and 2.26 MB resident, which is what the destructor running looks like.

## Licence

ISC.
