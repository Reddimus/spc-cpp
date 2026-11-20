"""Exercise the explicit-type audit through its command-line interface."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("cpp_auto_audit.py")


class AutoAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="builder-audit-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)

    def audit(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), "--repo-root", str(self.root), *args], capture_output=True, text=True, check=False)

    def test_builder_parent_does_not_hide_array_value(self):
        (self.root / "value.cpp").write_text("auto value = values[0];\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("value.cpp:1", result.stderr)

    def test_builder_sources_are_not_build_artifacts(self):
        folder = self.root / "src/buildsys"
        folder.mkdir(parents=True)
        (folder / "builder.cpp").write_text("auto value = 42;\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/buildsys/builder.cpp:1", result.stderr)

    def test_conditional_scalar_initializers_are_checked(self):
        (self.root / "condition.cpp").write_text("if (auto value = compute(); value) {}\nswitch (auto value = compute(); value) {}\n} else if (auto value = compute(); value) {}\nwhile (auto value = compute()) {}\nif constexpr (auto value = compute(); value) {}\n")
        result = self.audit()
        self.assertEqual(result.returncode, 1)
        self.assertIn("condition.cpp:1", result.stderr)
        self.assertIn("condition.cpp:2", result.stderr)
        self.assertIn("condition.cpp:3", result.stderr)
        self.assertIn("condition.cpp:4", result.stderr)
        self.assertIn("condition.cpp:5", result.stderr)

    def test_lambda_is_allowed_and_deleted_source_is_skipped(self):
        source = self.root / 'lambda café.cpp'
        subprocess.run(["git", "-C", str(self.root), "config", "core.quotePath", "true"], check=True)
        source.write_text("auto value = 42;\n")
        rejected = self.audit()
        self.assertEqual(rejected.returncode, 1)
        self.assertIn(source.name + ":1", rejected.stderr)
        source.write_text("auto fn = [](int n) { return n; };\n")
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        self.assertEqual(self.audit().returncode, 0)
        source.unlink()
        self.assertEqual(self.audit().returncode, 0)

    def test_nested_capture_brackets_are_allowed(self):
        (self.root / "nested.cpp").write_text('auto fn = [x = values[0], text = "]"]() { return x; };\n')
        self.assertEqual(self.audit().returncode, 0)

    def test_cpp23_lambda_specifiers_and_capture_literals_are_allowed(self):
        source = "auto fn = [n = 1'000, text = R\"tag(\" ] \")tag\"]() constexpr { return n; };\n"
        (self.root / "literal.cpp").write_text(source + "auto stateless = [] static { return 1; };\n")
        self.assertEqual(self.audit().returncode, 0)

    def test_immediately_invoked_lambdas_require_explicit_types(self):
        initializers = [
            "[]{ return 42; }()",
            "[] static { return 42; }()",
            "([] { return 42; })()",
            "(([] { return 42; }))()",
            "([&] { return values.begin() != values.end(); })()",
            "(([&] { return values.begin() != values.end(); })())",
            "[] { int pair[2]{1, 2}; auto [a, b] = pair; return a + b; }()",
            "[] { return values.begin() != values.end(); }()",
            "[] { return values.begin(); }() == values.end()",
            "[] { return std::distance(values.begin(), values.end()); }()",
            "[](int n) constexpr noexcept -> int { return n; }(42)",
            "[n = values[0]] { return n; } /* call */ ()",
            "[]\n{\n    if (true) { return 42; }\n    return 0;\n}\n()",
            '[] { const char* text = "}();"; return 42; }()',
            '[] { const char* text = R"tag(}\n(); ")tag"; return 42; }()',
            "[] { /* } */ // }\n    return 42;\n}()",
            "[] { return [] { return 42; }(); }()",
        ]
        for initializer in initializers:
            with self.subTest(initializer=initializer):
                (self.root / "lambda.cpp").write_text(f"auto value = {initializer};\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("lambda.cpp:1", result.stderr)

    def test_multiline_lambda_closures_preserve_body_literals_and_specifiers(self):
        initializers = [
            "[] static { return 42; }",
            "([] { return 42; })",
            "(([] { return 42; }))",
            "[] { int pair[2]{1, 2}; auto [a, b] = pair; return a + b; }",
            "[]() mutable noexcept { return 42; }",
            "[]() consteval -> int { return 42; }",
            "[]<typename T>(T n) constexpr noexcept -> T { return n; }",
            "[]<int N = 1 << 2>() { return N; }",
            "[]<typename T>(T n) requires true && requires { n + n; } { return n; }",
            "[]<typename T> requires true (T n) requires true { return n; }",
            "[]<typename T = decltype([] { return 42; })>() { return T{}; }",
            "[]<typename T>(T n) requires requires { n + n; } { return n; }",
            "[]\n{\n    if (true) { return 42; }\n    return 0;\n}",
            '[text = R"tag(]\n\" [)tag"]() { return text; }',
            '[] { const char* text = "}();"; return text; }',
            '[] { const char* text = R"tag(}\n(); ")tag"; return text; }',
            "[] { /* } */ // }\n    return 1'000;\n}",
            "[] { return [] { return 42; }(); }",
        ]
        for initializer in initializers:
            with self.subTest(initializer=initializer):
                (self.root / "lambda.cpp").write_text(f"auto fn = {initializer};\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_direct_iterator_returns_from_invoked_lambdas_are_allowed(self):
        for expression in ["values.begin()", "(values.begin())", "std::begin(values)", "std::next(values.begin())"]:
            for initializer in [
                f"[&] {{ return {expression}; }}()",
                f"([&] {{ return {expression}; }})()",
                f"([&] {{ return {expression}; }}())",
                f"(([&] {{ return {expression}; }})())",
            ]:
                with self.subTest(initializer=initializer):
                    (self.root / "iterator.cpp").write_text(f"auto it = {initializer};\n")
                    result = self.audit()
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_placeholder_forms_and_mid_line_declarations_require_types(self):
        sources = [
            "decltype(auto) value = compute();",
            "static decltype(auto) value = compute();",
            "std::integral auto value = 42;",
            "const std::convertible_to<std::vector<int>> auto value = 42;",
            "int a = 1; auto b = 2;",
            "int f() { auto x = g(); return x; }",
            "struct S { static constexpr auto value = 42; };",
            "auto fn = [] { auto x = 1; return x; };",
            "auto it = values.begin(); auto n = 5;",
            "for (int i = 0; auto& value : values) {}",
            "int* p = new auto(42);",
            "int* p = new const auto{42};",
            "int* p = new (buffer) auto(42);",
        ]
        for source in sources:
            with self.subTest(source=source):
                (self.root / "forms.cpp").write_text(source + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("forms.cpp:1", result.stderr)

    def test_iterator_call_must_be_the_whole_initializer(self):
        declarations = [
            "auto pos = text.find(',');",
            'auto pos = text.find("ab");',
            "auto pos = text.find(separator, 2);",
            "auto pos = [&] { return text.find(','); }();",
            "auto size = std::distance(v.begin(), v.end());",
            "auto found = table.find(key) != table.end();",
            "auto first = *values.begin();",
            "auto value = lookup(values.begin());",
            "auto next = values.begin() + 1;",
            "auto value = values.begin()->second;",
            "auto value = std::ranges::find(values, 42);",
            "auto it(values.size());",
            "auto value = values.begin()();",
            "auto value =\n    lookup(values.begin());",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                (self.root / "iterator.cpp").write_text(declaration + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("iterator.cpp:1", result.stderr)

    def test_whole_iterator_initializers_remain_allowed(self):
        declarations = [
            "auto it = m.find(k);",
            "auto it = m.find(make_key(a, b));",
            "auto it = (m.find(k));",
            "auto it{values.begin()};",
            "auto it(values.cend());",
            "auto it = config.layers().find(id);",
            "auto it = this->items_.lower_bound(key);",
            "auto range = table.equal_range(key);",
            "auto it = std::find(values.begin(), values.end(), 42);",
            "auto it = std::next(values.begin(), 2);",
            "auto it = std::max_element(values.begin(), values.end());",
            "auto a = values.begin(), b = values.end();",
            "for (auto it = values.begin(); it != values.end(); ++it) {}",
            "if (auto it = m.find(k); it != m.end()) {}",
            "auto it = m.find(k); auto other = m.find(j);",
            "auto it =\n    m.upper_bound(k);",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                (self.root / "iterator.cpp").write_text(declaration + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_parameters_bindings_and_lambdas_remain_allowed(self):
        sources = [
            "for (auto& [k, v] : m) {}",
            "auto f = [](int x) { return x; };",
            "const auto& [a, b] = pair;",
            "int f() { auto [a, b] = pair; return a; }",
            "auto f = [](auto x, const auto& y) { return x + y; };",
            "auto f = [](std::integral auto x) { return x; };",
            "auto f = []() -> decltype(auto) { return 42; };",
            "auto f = []() -> const auto& { return value; };",
            "void g(std::integral auto value, auto&& other);",
            "template <auto N> int h();",
            "template <typename T, std::integral auto N> int h();",
        ]
        for source in sources:
            with self.subTest(source=source):
                (self.root / "allowed.cpp").write_text(source + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_structured_binding_declarations_remain_allowed(self):
        declarations = [
            "auto [a, b] = pair;",
            "const auto& [a, b] = pair;",
            "if (auto [a, b] = pair; a) {}",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                (self.root / "binding.cpp").write_text("int pair[2]{1, 2};\n" + declaration + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_exemptions_in_literals_and_comments_do_not_hide_values(self):
        sources = [
            'auto value = std::string("values.begin(");',
            'auto value = std::string("auto-ok");',
            'auto value = std::string("auto [a, b] = pair;");',
            'auto value = std::string("[] { return 42; }");',
            'auto value = 42; // values.begin(',
            'auto /* declaration */ value = 42;',
            '/* auto [a, b] = pair; */ auto value = 42;',
            'auto value = 42; // [] { return 42; }',
            'auto value = [] {\n    // auto-ok: an inner line only\n    return 42;\n}();',
            'auto value = 42; /* auto [a, b] = pair; */',
            'auto value = [] { const char* text = "auto-ok"; return 42; }();',
            'auto value = [] { const char* text = "auto [a, b] = pair;"; return 42; }();',
            'auto value = [] { const char* text = R"tag(// auto-ok)tag"; return 42; }();',
            'auto value = [] { /* auto [a, b] = pair; */ return 42; }();',
        ]
        for source in sources:
            with self.subTest(source=source):
                (self.root / "scope.cpp").write_text(source + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("scope.cpp:1", result.stderr)

    def test_explicit_comment_opt_outs_remain_allowed(self):
        sources = [
            'auto value = 42; // auto-ok: generated contract',
            '/* auto-ok: generated contract */ auto value = 42;',
            'auto value = 42; /* auto-ok: generated contract */',
            'auto value = 42; /* auto-ok: generated\ncontract */',
            'auto value = [] { return 42; }(); // auto-ok: scalar result',
        ]
        for source in sources:
            with self.subTest(source=source):
                (self.root / "scope.cpp").write_text(source + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_multiline_literals_and_comments_are_not_declarations(self):
        (self.root / "scope.cpp").write_text(
            'const char* text = R"tag(\nauto ignored = 42;\n\vauto ignored = 42;\f)tag";\n'
            '/*\nauto ignored = 42;\n*/\n'
            'auto fn = /* capture follows */ [] { return 42; };\n'
            'auto it = values.begin();\n'
        )
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_multiline_initializers_keep_original_declaration_lines(self):
        source = (
            'const char* label = "café";\r\n'
            'auto fn =\r\n'
            '    [] static { return 42; };\r\n'
            'auto value =\r\n'
            '    [] static { return 42; }();\r\n'
            'auto it =\r\n'
            '    [&] { return values.begin(); }();\r\n'
        )
        (self.root / "offset.cpp").write_bytes(source.encode("utf-8"))
        result = self.audit()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(
            result.stderr,
            "cpp_auto_audit: disallowed local auto usage found\n"
            "offset.cpp:4: auto value =\n",
        )

    def test_scalar_reference_and_pointer_declarations_require_types(self):
        declarations = [
            "auto&& value = 42;",
            "auto** value = pointers;",
            "auto*** value = &pointers;",
            "const auto&& value = 42;",
            "[[maybe_unused]] auto value = 42;",
            "alignas(8) auto value = 42;",
            "alignas(alignof(int)) auto value = 42;",
            "alignas(alignof(int) == 4 ? 8 : 16) auto value = [] { return 42; }();",
            "auto ((value)) = 42;",
            "auto (value) = [] { return 42; }();",
            "thread_local auto value = 42;",
            "static constinit auto value = 42;",
            "extern auto value = 42;",
            "auto extern value = 42;",
            "inline auto value = 42;",
            "auto*& value = pointer;",
            "auto* const* value = pointers;",
            "auto const* const* value = pointers;",
            "auto volatile* value = pointer;",
            "auto&& value = [] { return 42; }();",
            "auto** value = [] { return pointers; }();",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                (self.root / "prefix.cpp").write_text(declaration + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("prefix.cpp:1", result.stderr)

    def test_reference_and_pointer_exceptions_remain_allowed(self):
        declarations = [
            "auto&& fn = [] { return 42; };",
            "auto const& fn = [] { return 42; };",
            "[[maybe_unused]] auto&& fn = [] { return 42; };",
            "alignas(8) auto fn = [] { return 42; };",
            "alignas(alignof(int) == 4 ? 8 : 16) auto fn = [] { return 42; };",
            "static constinit auto fn = [] { return 42; };",
            "thread_local auto fn = [] { return 42; };",
            "auto constinit static fn = [] { return 42; };",
            "auto extern fn = [] { return 42; };",
            "auto inline fn = [] { return 42; };",
            "extern auto fn = [] { return 42; };",
            "inline auto fn = [] { return 42; };",
            "auto&& [a, b] = pair;",
            "auto const& [a, b] = pair;",
            "auto&& it = values.begin();",
            "auto* it = std::begin(pair);",
            "auto* const it = std::begin(pair);",
            "auto** it = std::begin(pointer_array);",
            "auto* volatile it = std::begin(pair);",
            "auto&& it = table.find(0);",
            "auto static it = table.find(0);",
            "auto thread_local it = table.find(0);",
            "auto const& it = table.find(0);",
            "auto (it) = table.find(0);",
            "auto ((it)) = table.find(0);",
        ]
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                (self.root / "prefix.cpp").write_text(declaration + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_generic_parameters_are_not_local_declarations(self):
        sources = [
            "auto fn = [](\n    auto&& end) { return end; };",
            "auto fn = [](\n    auto const& end,\n    auto&& other) { return end; };",
            "auto fn = [](\n    auto&& end [[maybe_unused]]) { return end; };",
        ]
        for source in sources:
            with self.subTest(source=source):
                (self.root / "generic.cpp").write_text(source + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_stray_quotes_in_skipped_text_do_not_hide_declarations(self):
        for message in ["this isn't code", '"unfinished message', "#error compiler's diagnostic"]:
            source = (
                "#if 0\n" + message + "\n#endif\nauto value = 42;\n"
                "char marker = 'x';\nconst char* text = \"ok\";\n"
            )
            (self.root / "quotes.cpp").write_text(source)
            result = self.audit()
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertEqual(
                result.stderr,
                "cpp_auto_audit: disallowed local auto usage found\n"
                "quotes.cpp:4: auto value = 42;\n",
            )

    def test_escaped_literal_continuations_remain_literals(self):
        source = (
            'const char* text = "\\\nauto ignored = 42;";\n'
            "char letter = '\\\nX';\n"
            'auto fn = [] { return 42; };\n'
        )
        (self.root / "quotes.cpp").write_text(source)
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_allowlist_regeneration_is_idempotent(self):
        (self.root / "value.cpp").write_text("auto value = 42;\n")
        self.assertEqual(self.audit("--write-allowlist").returncode, 0)
        first = (self.root / "tools/cpp_auto_allowlist.txt").read_text()
        self.assertIn("value.cpp|auto value = 42;", first)
        self.assertEqual(self.audit("--write-allowlist").returncode, 0)
        self.assertEqual((self.root / "tools/cpp_auto_allowlist.txt").read_text(), first)
        self.assertEqual(self.audit().returncode, 0)


if __name__ == "__main__":
    unittest.main()
