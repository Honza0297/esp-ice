# ldgen -- Linker Fragment Parser

Parses ESP-IDF linker fragment files (`.lf`).  The grammar below is
extracted from the pyparsing definitions in
`tools/ldgen/ldgen/fragments.py` (`class Fragment`, `class Sections`,
`class Scheme`, `class Mapping`, `parse_fragment_file()`).

## Lexical elements

Four distinct name classes are used in different positions:

    IDENT    = [a-zA-Z_] [a-zA-Z0-9_]*             Fragment.IDENTIFIER
    ENTITY   = [a-zA-Z0-9.\-_$+]+                  Fragment.ENTITY
    SEC_NAME = [a-zA-Z_.] [a-zA-Z0-9._-]* '+'?     Sections.ENTRY (Combine)
    OBJ_NAME = [a-zA-Z_] [a-zA-Z0-9\-_]*           Mapping._obj

Other terminals:

    NUM      = [0-9]+
    EXPR     = [^:\n]+                              SkipTo(':') in get_conditional_stmt()
    SORT_KEY = 'name' | 'alignment' | 'init_priority'

Our lexer unifies the four name classes into a single NAME token
covering their union.  The parser relies on context to distinguish them.

### Indentation

pyparsing's `IndentedBlock(_stmt)` groups statements whose leading
whitespace is >= the first statement's indent.  Our lexer emits
INDENT / DEDENT tokens with the same semantics.

Comments (`#` to end of line) and blank lines are transparent to
indentation — they neither set nor break a block level.

## Productions

### File

    file        = { fragment }
    fragment    = sections | scheme | mapping | cond(fragment)

### Sections

    sections    = '[sections:' IDENT ']' NL
                  'entries:' suite(sec_stmt)

    sec_stmt    = SEC_NAME NL
                | cond(sec_stmt)

### Scheme

    scheme      = '[scheme:' IDENT ']' NL
                  'entries:' suite(sch_stmt)

    sch_stmt    = IDENT '->' IDENT NL
                | cond(sch_stmt)

### Mapping

    mapping     = '[mapping:' IDENT ']'
                  'archive:' suite(archive_stmt)
                  'entries:' suite(map_stmt)

    archive_stmt = (ENTITY | '*') NL
                 | cond(archive_stmt)

    map_stmt    = map_entry NL
                | map_entry ';' flag_list
                | cond(map_stmt)

    map_entry   = (OBJ_NAME [ ':' IDENT ] | '*') '(' IDENT ')'

Note: `Mapping.parse_archive()` enforces that the archive suite
resolves to exactly one value after conditional evaluation.

### Flags

    flag_list   = flag_item { ',' flag_item }       DelimitedList in Mapping.ENTRY_WITH_FLAG
    flag_item   = IDENT '->' IDENT flag { flag }    Flag.FLAG (OneOrMore)
    flag        = 'KEEP()'                          Keep.KEEP (single Keyword)
                | 'ALIGN' '(' NUM [',' 'pre'] [',' 'post'] ')'
                                                    Align.ALIGN (order matters: pre before post)
                | 'SORT' '(' [ SORT_KEY [',' SORT_KEY] ] ')'
                                                    Sort.SORT
                | 'SURROUND' '(' IDENT ')'          Surround.SURROUND

### Conditionals

    cond(S)     = 'if' EXPR ':' suite({ S })
                  { 'elif' EXPR ':' suite({ S }) }
                  [ 'else:' suite({ S }) ]

`else:` is a single literal (`Literal('else:')` in `get_conditional_stmt()`),
not two tokens.

### Suite

    suite(S)    = IndentedBlock(S | comment | cond(S))

A suite is an indented block built by `get_suite()`.  The first content
line sets the indent level; subsequent lines must be at the same or
deeper level.  The block ends when a line appears at a shallower level.

## LL(1) decision points

| After seeing         | Lookahead     | Choose                |
|----------------------|---------------|-----------------------|
| `entries:` NL        | INDENT        | indented block        |
| `entries:` NL        | NAME / `*`    | same-level block      |
| `archive:` ...       | NAME / `*`    | inline value          |
| `archive:` ...       | NL            | indented block        |
| NAME in map_entry    | `(`           | object (scheme)       |
| NAME in map_entry    | `:`           | object:symbol (scheme)|
| `)` in map_entry     | `;`           | entry with flags      |
| `)` in map_entry     | NL            | plain entry           |
| NAME in stmts        | NAME is `if`  | conditional           |
| NAME after DEDENT    | NAME is `elif`| continue conditional  |
| NAME after DEDENT    | NAME is `else`| else branch           |
| NAME after DEDENT    | other         | conditional done      |
