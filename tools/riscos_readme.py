#!/usr/bin/env python3
"""
ArchiLudo RISC OS plain-text README generator
==============================================

Round 7.90. Live-tested on real hardware: the plain-text README (built via
a straight `pandoc README.md -t plain`) did not "correctly paginate" --
traced to `pandoc`'s `plain` writer rendering Markdown pipe-tables as
fixed-width grid tables sized to the longest cell (this project's own
"Prerequisites"/"Make targets" tables, per the "Building from source"
section convention in `~/.claude/makefile_conventions.md`, both run to
~170 characters wide once rendered), regardless of `--columns`. On a
RISC OS text display (traditionally far narrower, and RISC OS's own
`*Show`/paged-mode text scrolling has no line-wrap awareness -- it
advances a fixed number of *logical* lines per screen, not visual rows),
a 170-character line blows straight through that budget.

Fix: flatten each Markdown pipe-table into a stacked "Header: value"
record per row (one field per line) BEFORE handing the document to
pandoc, so nothing pandoc emits is wider than the prose it already wraps
correctly with `--columns`. Everything else in the document (headings,
prose, lists, code fences) passes through untouched -- this only
touches pipe-table blocks specifically.
"""

import re
import subprocess
import sys
import textwrap

PANDOC_COLUMNS = 78
"""Comfortably under RISC OS's traditional 80-column text display, with
a small margin so nothing sits flush against the last column."""

TABLE_ROW_RE = re.compile(r"^\|(.+)\|\s*$")
TABLE_SEPARATOR_RE = re.compile(r"^\|(\s*:?-+:?\s*\|)+\s*$")
TABLE_PLACEHOLDER = "@@ARCHILUDO-TABLE-{}@@"
"""A flattened table is rendered directly to final plain text (see
flatten_table) rather than re-encoded as Markdown and handed back to
pandoc -- round-tripping cell content that may itself contain backslashes
or underscores (e.g. a literal Windows path) back through a Markdown
parser corrupts it (backslash-escaping, accidental emphasis markers).
Instead each table becomes one placeholder line pandoc passes through
untouched, substituted back in after pandoc has wrapped everything else."""


def split_row(line: str) -> list[str]:
    """Split a Markdown table row into its cell strings, honouring
    backtick-quoted cells that may themselves contain literal '|'."""
    inner = line.strip()[1:-1]  # drop the leading/trailing '|'
    cells: list[str] = []
    cell = ""
    in_code = False
    i = 0
    while i < len(inner):
        ch = inner[i]
        if ch == "`":
            in_code = not in_code
            cell += ch
        elif ch == "\\" and i + 1 < len(inner):
            cell += ch + inner[i + 1]
            i += 1
        elif ch == "|" and not in_code:
            cells.append(cell.strip())
            cell = ""
        else:
            cell += ch
        i += 1
    cells.append(cell.strip())
    return cells


ASCII_TRANSLITERATIONS = {
    "–": "-",   # en dash
    "—": "--",  # em dash
    "‘": "'", "’": "'",   # curly single quotes
    "“": '"', "”": '"',   # curly double quotes
    "…": "...",  # ellipsis
    " ": " ",   # non-breaking space
}
"""RISC OS text files are single-byte (traditionally its own Latin-1-like
8-bit charset), not UTF-8 -- pandoc's plain writer emits UTF-8 "smart"
typography (curly quotes, en/em dashes) for Markdown's own straight-quote
and `--`/`---` conventions, which would come out as raw multi-byte
garbage on real hardware if left alone. Transliterated to plain ASCII
rather than mapped to RISC OS's actual high-byte codepoints for those
glyphs, since plain ASCII is unambiguously correct regardless of which
8-bit RISC OS variant or screen font is in use."""


def to_ascii(text: str) -> str:
    for char, replacement in ASCII_TRANSLITERATIONS.items():
        text = text.replace(char, replacement)
    non_ascii = sorted({c for c in text if ord(c) > 127})
    if non_ascii:
        raise ValueError(
            "README.md contains characters with no ASCII transliteration "
            "registered in ASCII_TRANSLITERATIONS: "
            + ", ".join(f"{c!r} (U+{ord(c):04X})" for c in non_ascii)
        )
    return text


def strip_markdown_inline(text: str) -> str:
    """Drop the Markdown formatting plain-text readers don't need:
    link syntax and backtick code spans, keeping the visible text."""
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)  # [text](url)
    text = text.replace("`", "")
    return text


def flatten_table(lines: list[str]) -> str:
    """Render one Markdown pipe-table (header + separator + rows) directly
    to final plain text: stacked "Header: value" records, one field per
    line (wrapped with a hanging indent if a value is long), blank line
    between records."""
    header = [strip_markdown_inline(c) for c in split_row(lines[0])]
    records: list[str] = []
    for row_line in lines[2:]:
        cells = [strip_markdown_inline(c) for c in split_row(row_line)]
        for name, value in zip(header, cells):
            if not value:
                records.append(f"{name}:")
                continue
            wrapped = textwrap.wrap(
                f"{name}: {value}",
                width=PANDOC_COLUMNS,
                subsequent_indent="    ",
                break_long_words=False,
                break_on_hyphens=False,
            )
            records.append("\n".join(wrapped) if wrapped else f"{name}:")
        records.append("")
    return "\n".join(records)


def extract_tables(markdown: str) -> tuple[str, list[str]]:
    """Replace each Markdown pipe-table with a placeholder line, returning
    the placeholder-bearing markdown (safe to hand to pandoc) and the list
    of already-rendered plain-text replacements, in order."""
    lines = markdown.splitlines()
    out: list[str] = []
    tables: list[str] = []
    i = 0
    while i < len(lines):
        if (
            TABLE_ROW_RE.match(lines[i])
            and i + 1 < len(lines)
            and TABLE_SEPARATOR_RE.match(lines[i + 1])
        ):
            j = i + 2
            while j < len(lines) and TABLE_ROW_RE.match(lines[j]):
                j += 1
            tables.append(flatten_table(lines[i:j]))
            out.append("")
            out.append(TABLE_PLACEHOLDER.format(len(tables) - 1))
            out.append("")
            i = j
        else:
            out.append(lines[i])
            i += 1
    return "\n".join(out) + "\n", tables


def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: riscos_readme.py <input.md> <output>", file=sys.stderr)
        sys.exit(1)
    src, dest = sys.argv[1], sys.argv[2]

    with open(src, encoding="utf-8") as f:
        markdown = f.read()

    placeheld_markdown, tables = extract_tables(markdown)

    result = subprocess.run(
        ["pandoc", "-t", "plain", f"--columns={PANDOC_COLUMNS}"],
        input=placeheld_markdown,
        capture_output=True,
        text=True,
        check=True,
    )

    plain = result.stdout
    for index, table_text in enumerate(tables):
        plain = plain.replace(TABLE_PLACEHOLDER.format(index), table_text)

    plain = to_ascii(plain)

    with open(dest, "w", encoding="ascii", newline="\r") as f:
        f.write(plain)


if __name__ == "__main__":
    main()
