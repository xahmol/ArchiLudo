--[[
Pandoc Lua filter: make long inline `code` spans breakable in PDF output.

Why this exists: pandoc's LaTeX writer renders inline `code` as
\texttt{...}, and \texttt text never hyphenates by default -- a single
long unbroken token (a filesystem path, a long filename, a URL with no
spaces) inside a table cell or a narrow column just runs past the page
margin instead of wrapping. This was found in README.pdf's "Building
from source" tables (a git-clone one-liner, a Windows hostfs path) --
see docs/BUILDCHAIN.md's "PDF generation" note.

Fix: wrap a space-less code span in \seqsplit{} (the seqsplit package,
enabled via the Makefile's --lua-filter + -H invocation), which allows
a break at ANY character -- purpose-built for exactly this (paths,
URLs, hashes, identifiers with no natural word-break point). A code
span that DOES contain spaces (e.g. a multi-word shell command) is left
as plain \texttt{...}: it already wraps fine at those spaces once
inside a normal paragraph or table cell, and \seqsplit actively
swallows literal space characters, which would corrupt a multi-word
command instead of just letting it wrap normally.

Usage: pandoc ... --lua-filter=tools/pandoc_wrap_code.lua -H <file with
\usepackage{seqsplit}> -o out.pdf
(wired into the Makefile's README.pdf target -- see docs/BUILDCHAIN.md)

Requires the seqsplit LaTeX package (Debian/Ubuntu: apt package
texlive-latex-extra, NOT texlive-xetex -- confirmed via
`dpkg -S seqsplit.sty`).
--]]

-- Single-pass substitution table -- doing this as separate sequential
-- gsub calls (escape backslash, THEN escape braces, ...) is wrong: the
-- literal "{}" that the backslash replacement inserts gets caught and
-- re-escaped by the very next pass. One gsub with a lookup table avoids
-- the multi-pass self-interference entirely.
local special = {
	['\\'] = '\\textbackslash{}',
	['{'] = '\\{',
	['}'] = '\\}',
	['#'] = '\\#',
	['$'] = '\\$',
	['%'] = '\\%',
	['&'] = '\\&',
	['_'] = '\\_',
	['^'] = '\\textasciicircum{}',
	['~'] = '\\textasciitilde{}',
}
local function latex_escape_code(s)
	return (s:gsub('[\\{}#$%%&_%^~]', special))
end

function Code(el)
	if not FORMAT:match('latex') then
		return nil
	end

	local escaped = latex_escape_code(el.text)

	if el.text:find('%s') then
		return pandoc.RawInline('latex', '\\texttt{' .. escaped .. '}')
	end

	-- \texttt OUTSIDE, \seqsplit INSIDE: the reverse nesting silently
	-- drops the monospace font entirely (seqsplit processes its
	-- argument character-by-character and doesn't preserve an active
	-- font switch that isn't already in effect when it starts).
	return pandoc.RawInline('latex', '\\texttt{\\seqsplit{' .. escaped .. '}}')
end
