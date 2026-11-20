#!/usr/bin/env python3
"""Fail when C++ sources use `auto` outside the allowed cases.

Allowed: structured bindings, lambda objects, iterators, and any line with an
`auto-ok` comment. Everything else spells out its type. Without type
information, an iterator is recognized only when the whole initializer is one
iterator-producing call, such as `v.begin()`, `m.find(k)` or `std::next(it)`.
Function and template parameters are not checked. The allowlist file takes
one `relative/path.cpp|line snippet` rule per line, matched as a substring of
the stripped source line.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterator
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
import re


CPP_SUFFIXES = {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"}
DEFAULT_ALLOWLIST = "tools/cpp_auto_allowlist.txt"
WORD_AUTO = re.compile(r"\bauto\b")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
DECL_SPECIFIERS = {
    "const", "constexpr", "static", "volatile", "mutable",
    "constinit", "thread_local", "extern", "inline",
}
# Words that may precede `auto` without being a type-constraint (concept) name.
NOT_CONSTRAINTS = DECL_SPECIFIERS | {
    "case", "co_await", "co_return", "co_yield", "consteval", "do", "else", "explicit",
    "friend", "new", "operator", "requires", "return", "template", "throw", "typedef",
    "typename", "using", "virtual",
}
CONDITION_KEYWORDS = {"for", "if", "switch", "while"}
PAIRS = {"(": ")", "[": "]", "{": "}"}
ITERATOR_MEMBERS = {
    "begin", "end", "cbegin", "cend", "rbegin", "rend", "crbegin", "crend",
    "find", "lower_bound", "upper_bound", "equal_range",
}
ITERATOR_FUNCTIONS = {  # in namespace std
    "begin", "end", "cbegin", "cend", "find", "lower_bound", "upper_bound",
    "equal_range", "max_element", "min_element", "prev", "next",
}


@dataclass(frozen=True)
class AllowRule:
    relative_path: str
    snippet: str


@dataclass(frozen=True)
class Violation:
    relative_path: str
    line_number: int
    line: str


def tracked_cpp_files(repo_root: Path) -> list[Path]:
    # Include untracked, non-ignored files so a new test file is audited
    # locally before CI sees it.
    proc = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    )
    files: list[Path] = []
    for raw in sorted(set(proc.stdout.split("\0")) - {""}):
        relative = Path(raw)
        if relative.suffix not in CPP_SUFFIXES:
            continue
        if any(part == "build" or part.startswith("build-") or part == "_deps" for part in relative.parts[:-1]):
            continue
        if not (repo_root / relative).exists():  # deleted but not yet staged
            continue
        files.append(repo_root / relative)
    return files


def read_allowlist(path: Path) -> list[AllowRule]:
    if not path.exists():
        return []
    rules: list[AllowRule] = []
    for raw in path.read_text().splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        relative_path, sep, snippet = stripped.partition("|")
        if not sep:
            raise ValueError(f"invalid allowlist rule: {raw!r}")
        rules.append(AllowRule(relative_path=relative_path.strip(), snippet=snippet.strip()))
    return rules


# Skip literals and comments before inspecting C++ delimiters. Keep numbers
# together so digit separators cannot be mistaken for character literals.
CPP_TOKEN = re.compile(
    r'(?P<skip>(?:u8|u|U|L)?R"(?P<raw>[^\s()\\]{0,16})\(.*?\)(?P=raw)"'
    r'|"(?:\\.|[^"\\\r\n])*"|\b\d[\w\'.]*|\'(?:\\.|[^\'\\\r\n])*\''
    r'|/\*.*?\*/|//[^\n]*)|[A-Za-z_]\w*|<<|[^\s]',
    re.DOTALL,
)


def group_end(tokens: list[str], start: int) -> int | None:
    """Index of the bracket that closes tokens[start]."""
    opener, closer = tokens[start], PAIRS[tokens[start]]
    depth = 0
    for index in range(start, len(tokens)):
        if tokens[index] == opener:
            depth += 1
        elif tokens[index] == closer:
            depth -= 1
            if depth == 0:
                return index
    return None


def group_start(tokens: list[str], end: int, opener: str) -> int | None:
    """Index of the `opener` that tokens[end] closes."""
    closer = tokens[end]
    depth = 0
    for index in range(end, -1, -1):
        if tokens[index] == closer:
            depth += 1
        elif tokens[index] == opener:
            depth -= 1
            if depth == 0:
                return index
    return None


def code_tokens(source: str, start: int) -> Iterator[str]:
    # Keep literals: find()'s arguments show whether it is a string search.
    for token in CPP_TOKEN.finditer(source, start):
        if not token[0].startswith(("//", "/*")):
            yield token[0]


def statement_tokens(source: str, start: int) -> list[str]:
    """Tokens through the `;` or unmatched closer that ends the statement."""
    tokens: list[str] = []
    depth = 0
    for text in code_tokens(source, start):
        tokens.append(text)
        if text in PAIRS:
            depth += 1
        elif text in PAIRS.values():
            depth -= 1
            if depth < 0:
                break
        elif text == ";" and depth == 0:
            break
    return tokens


def leading_expression(tokens: list[str]) -> list[str]:
    """Tokens before the first ungrouped `,` or `;`, or an unmatched closer."""
    depth = 0
    for index, text in enumerate(tokens):
        if text in PAIRS:
            depth += 1
        elif text in PAIRS.values():
            if depth == 0:
                return tokens[:index]
            depth -= 1
        elif text in {",", ";"} and depth == 0:
            return tokens[:index]
    return tokens


def is_text_literal(token: str) -> bool:
    return '"' in token or token.startswith("'")


def iterator_call(tokens: list[str]) -> bool:
    """Whether the whole expression is one call that yields an iterator.

    The call may end a member chain, as in `a.items().begin()`, but not be an
    operand or an argument. A type-free check cannot tell a container's find()
    from std::string's, so find() counts only with one argument and no string
    or character literal: string searches usually take a literal or a position.
    """
    while tokens and tokens[0] == "(" and group_end(tokens, 0) == len(tokens) - 1:
        tokens = tokens[1:-1]
    if not tokens or not IDENTIFIER.fullmatch(tokens[0]):
        return False
    callee = [tokens[0]]
    index = 1
    while (
        tokens[index:index + 2] == [":", ":"]
        and index + 2 < len(tokens)
        and IDENTIFIER.fullmatch(tokens[index + 2])
    ):
        callee.append(tokens[index + 2])
        index += 3
    member = False
    call: tuple[bool, list[str], list[str]] | None = None
    while index < len(tokens):
        if tokens[index] in {"(", "["}:
            end = group_end(tokens, index)
            if end is None:
                return False
            named_call = tokens[index] == "(" and callee
            call = (member, callee, tokens[index + 1:end]) if named_call else None
            callee = []
            index = end + 1
        elif tokens[index] == "." or tokens[index:index + 2] == ["-", ">"]:
            index += 1 if tokens[index] == "." else 2
            if index == len(tokens) or not IDENTIFIER.fullmatch(tokens[index]):
                return False
            member, callee, call = True, [tokens[index]], None
            index += 1
        else:
            return False
    if call is None:
        return False
    member, callee, arguments = call
    if not member:
        return len(callee) == 2 and callee[0] == "std" and callee[1] in ITERATOR_FUNCTIONS
    if callee != ["find"]:
        return callee[0] in ITERATOR_MEMBERS
    return (
        bool(arguments)
        and leading_expression(arguments) == arguments
        and not any(is_text_literal(token) for token in arguments)
    )


def placeholder_context(tokens: list[str], start: int) -> str:
    """Say whether the `auto` at tokens[start] begins a declaration statement.

    Returns "statement", "new" for a new-expression, or "other" for
    parameters, template arguments, trailing return types, and expressions.
    Only the current line is visible, so a line start counts as a statement.
    """
    index = start - 1
    # A type-constraint such as std::integral or std::convertible_to<int>.
    if index >= 1 and tokens[index] == ">" and tokens[index - 1] != "-":
        opening = group_start(tokens, index, "<")
        if (
            opening is not None
            and opening >= 1
            and IDENTIFIER.fullmatch(tokens[opening - 1])
            and tokens[opening - 1] not in NOT_CONSTRAINTS
        ):
            index = opening - 1
    if index >= 0 and IDENTIFIER.fullmatch(tokens[index]) and tokens[index] not in NOT_CONSTRAINTS:
        index -= 1
        while index >= 1 and tokens[index] == tokens[index - 1] == ":":
            index -= 2
            if index < 0 or not IDENTIFIER.fullmatch(tokens[index]):
                break
            index -= 1
    while index >= 0:
        if tokens[index] in DECL_SPECIFIERS:
            index -= 1
        elif tokens[index] == "]" and index >= 1 and tokens[index - 1] == "]":
            opening = group_start(tokens, index, "[")
            if opening is None:
                break
            index = opening - 1
        elif tokens[index] == ")":
            opening = group_start(tokens, index, "(")
            if opening is None or opening == 0 or tokens[opening - 1] != "alignas":
                break
            index = opening - 2
        else:
            break
    if index < 0 or tokens[index] in {";", "{", "}", "else", "do"}:
        return "statement"
    previous = tokens[index]
    if previous == ":" and (index == 0 or tokens[index - 1] != ":"):
        return "statement"  # a label or access specifier
    if previous == "new":
        return "new"
    if previous == ")":
        opening = group_start(tokens, index, "(")
        if opening is not None and opening >= 1 and tokens[opening - 1] == "new":
            return "new"  # placement new
    if previous == "(":
        keyword = index - 1
        if keyword >= 0 and tokens[keyword] == "constexpr":
            keyword -= 1
        if keyword >= 0 and tokens[keyword] in CONDITION_KEYWORDS:
            return "statement"
    return "other"


def declaration_allowed(tokens: list[str]) -> bool:
    """Apply the rules to the tokens that follow a declaration's `auto`."""
    index = 0
    parentheses = 0
    while index < len(tokens) and (tokens[index] in {"*", "&", "("} or tokens[index] in DECL_SPECIFIERS):
        parentheses += tokens[index] == "("
        index += 1
    if index == len(tokens) or not IDENTIFIER.fullmatch(tokens[index]):
        return True  # a structured binding, or an expression such as auto{x}
    index += 1
    while index < len(tokens) and tokens[index] == ")" and parentheses:
        parentheses -= 1
        index += 1
    while tokens[index:index + 2] == ["[", "["]:
        end = group_end(tokens, index)
        if end is None:
            return False
        index = end + 1
    following = tokens[index] if index < len(tokens) else None
    if following in {",", ")"}:
        return True  # a parameter continued from an earlier line
    if following == "=":
        initializer = tokens[index + 1:]
    elif following in {"(", "{"}:
        initializer = tokens[index + 1:group_end(tokens, index)]
    else:
        return False
    lambda_allowed = lambda_initializer_allowed(initializer)
    if lambda_allowed is not None:
        return lambda_allowed
    # Every declarator of one auto declaration deduces the same type, so the
    # first initializer decides.
    return iterator_call(leading_expression(initializer))


def lexical_context(source: str) -> tuple[str, set[int]]:
    """Mask literals/comments without moving code, and locate comment opt-outs."""
    pieces: list[str] = []
    opt_out_lines: set[int] = set()
    position = 0
    line_number = 1
    for token in CPP_TOKEN.finditer(source):
        gap = source[position:token.start()]
        pieces.append(gap)
        line_number += gap.count("\n")
        text = token[0]
        if token.lastgroup == "skip":
            if text.startswith(("//", "/*")):
                for number, comment_line in enumerate(text.split("\n"), start=line_number):
                    if "auto-ok" in comment_line:
                        opt_out_lines.add(number)
            pieces.append(re.sub(r"[^\n\r]", " ", text))
        else:
            pieces.append(text)
        line_number += text.count("\n")
        position = token.end()
    pieces.append(source[position:])
    return "".join(pieces), opt_out_lines


def direct_iterator_return(body: list[str]) -> bool:
    # Recognize a single returned iterator call, not incidental calls elsewhere
    # in a body or an iterator comparison whose result is a bool.
    return len(body) > 2 and body[0] == "return" and body[-1] == ";" and iterator_call(body[1:-1])


def lambda_initializer_allowed(initializer: list[str]) -> bool | None:
    """Return None for another initializer, otherwise apply lambda/iterator rules."""
    tokens = iter(initializer)
    first = next(tokens, None)
    parentheses = 0
    while first == "(":
        parentheses += 1
        first = next(tokens, None)
    if first != "[":
        return None

    def consume_group(opener: str, contents: list[str] | None = None) -> bool:
        stack = [">" if opener == "<" else PAIRS[opener]]
        for token in tokens:
            if token in PAIRS:
                stack.append(PAIRS[token])
            elif token == "<" and stack[-1] == ">":
                stack.append(">")
            elif token == ">" and stack[-1] == ">":
                stack.pop()
                if not stack:
                    return True
            elif token in PAIRS.values():
                if token != stack.pop():
                    return False
                if not stack:
                    return True
            if contents is not None:
                contents.append(token)
        return False

    if not consume_group("["):
        return False
    # Parameter lists, attributes and noexcept/decltype expressions may contain
    # braces. The closure body starts at the next ungrouped opening brace.
    first_header_token = True
    requires_expression = False
    previous = None
    for token in tokens:
        if token == "requires" and previous in {"requires", "&", "|", "!", "?", ":"}:
            requires_expression = True
        if token == "{":
            if requires_expression:
                if not consume_group("{"):
                    return False
                requires_expression = False
                previous = token
                continue
            body: list[str] = []
            if not consume_group("{", body):
                return False
            following = next(tokens, None)
            invoked = False
            while True:
                if following == ")" and parentheses:
                    parentheses -= 1
                    following = next(tokens, None)
                elif following == "(" and not invoked:
                    if not consume_group("("):
                        return False
                    invoked = True
                    following = next(tokens, None)
                else:
                    break
            if parentheses or following not in {None, ";", ",", ")", "}"}:
                return False
            return not invoked or direct_iterator_return(body)
        if token in {"(", "["} or (first_header_token and token == "<"):
            if not consume_group(token):
                return False
        elif token in {";", "}", "]", ")"}:
            return False
        first_header_token = False
        previous = token
    return False


def line_allowed(code_line: str, source: str, line_offset: int) -> bool:
    """Check every `auto` on a line whose literals and comments are blanked."""
    matches = list(CPP_TOKEN.finditer(code_line))
    tokens = [match[0] for match in matches]
    for index, token in enumerate(tokens):
        if token != "auto":
            continue
        start = end = index
        if index >= 2 and tokens[index - 2:index] == ["decltype", "("] and tokens[index + 1:index + 2] == [")"]:
            start, end = index - 2, index + 1
        context = placeholder_context(tokens, start)
        if context == "new":
            return False
        if context == "statement" and not declaration_allowed(
            statement_tokens(source, line_offset + matches[end].end())
        ):
            return False
    return True


def matches_allowlist(relative_path: str, line: str, rules: list[AllowRule]) -> bool:
    stripped = line.strip()
    for rule in rules:
        if rule.relative_path == relative_path and rule.snippet in stripped:
            return True
    return False


def collect_violations(repo_root: Path, allowlist: list[AllowRule]) -> list[Violation]:
    violations: list[Violation] = []
    for path in tracked_cpp_files(repo_root):
        relative_path = path.relative_to(repo_root).as_posix()
        source = path.read_text()
        code, opt_out_lines = lexical_context(source)
        offset = 0
        for line_number, line in enumerate(source.split("\n"), start=1):
            line_offset = offset
            offset += len(line) + 1
            code_line = code[line_offset:line_offset + len(line)]
            if not WORD_AUTO.search(code_line):
                continue
            if line_number in opt_out_lines or line_allowed(code_line, source, line_offset):
                continue
            if matches_allowlist(relative_path, line, allowlist):
                continue
            violations.append(
                Violation(relative_path=relative_path, line_number=line_number, line=line.rstrip())
            )
    return violations


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    parser.add_argument("--write-allowlist", action="store_true")
    return parser.parse_args()


def write_allowlist(path: Path, violations: list[Violation]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Exceptions to the explicit-type rule, one per line:",
        "# relative/path.cpp|line snippet",
    ]
    for violation in violations:
        lines.append(f"{violation.relative_path}|{violation.line.strip()}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    allowlist_path = repo_root / args.allowlist
    allowlist = [] if args.write_allowlist else read_allowlist(allowlist_path)
    violations = collect_violations(repo_root, [] if args.write_allowlist else allowlist)
    if args.write_allowlist:
        write_allowlist(allowlist_path, violations)
        return 0
    if not violations:
        print("cpp_auto_audit: no disallowed local auto usage found")
        return 0
    print("cpp_auto_audit: disallowed local auto usage found", file=sys.stderr)
    for violation in violations:
        print(
            f"{violation.relative_path}:{violation.line_number}: {violation.line.strip()}",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
