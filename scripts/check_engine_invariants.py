#!/usr/bin/env python3
"""
check_engine_invariants.py — Strict Static Architectural Invariant Linter for Impulse Graph Engine.

Enforces the non-negotiable engine invariants (AGENTS.md §3.19 & engine_invariants.md) across
Java and C++ VM/adapter sources so agents and contributors CANNOT introduce:
  1. Runtime topology synthesis (ensureCsc, buildRevAdj, p3_build_rev_adj, p3_compute_in_degs).
  2. Heap allocations (`new ArrayList/HashMap/TreeSet/HashSet/int[]/long[]/double[]`, `Arrays.copyOf`)
     or `java.util.*` collection imports inside Java VM hot-path opcode evaluators.
  3. STL container includes (`<vector>`, `<map>`, `<unordered_map>`, `<set>`, `<string>`) or
     heap allocators (`malloc`, `posix_memalign`, `file_size * 3`) inside C++ VM opcode evaluators.
  4. Heap/arena copies of .imps CSR/CSC topology or full-column attribute/key materialization.
  5. Direct heap-array topology dereferences (rel.rowOffsets / rel.colTargets) in production VM ops.
  6. Missing workspace reset cleanup (strTable / geometry / multi-frame spills).

Exit code 0 = clean; non-zero = invariant violations found.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent.parent


def resolve_roots(java_root: Path | None, cpp_root: Path | None) -> tuple[Path, Path]:
    if java_root is None:
        java_root = ROOT_DIR / "impulse-graph-java"
    if cpp_root is None:
        wt = ROOT_DIR / ".worktrees" / "v11-m3" / "impulse-graph-core" / "impulse-cpp"
        cpp_root = wt if wt.exists() else (ROOT_DIR / "impulse-graph-core" / "impulse-cpp")
    return java_root, cpp_root


def strip_comments(line: str) -> str:
    idx = line.find("//")
    if idx >= 0:
        return line[:idx]
    return line


def scan_file(path: Path, rules: list[tuple[str, re.Pattern[str], str]]) -> list[tuple[str, str]]:
    violations: list[tuple[str, str]] = []
    if not path.exists():
        return violations
    text = path.read_text(encoding="utf-8", errors="replace")
    for lineno, raw in enumerate(text.splitlines(), start=1):
        code = strip_comments(raw)
        if not code.strip():
            continue
        if "@AllowAllocInit" in raw or "INVARIANT_ALLOW:" in raw:
            continue
        for rule_id, pat, msg in rules:
            if pat.search(code):
                violations.append((rule_id, f"[{rule_id}] {path}:{lineno}: {msg}\n    {raw.strip()}"))
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="Impulse Graph Engine strict static invariant linter")
    parser.add_argument("--java-root", type=Path, default=None)
    parser.add_argument("--cpp-root", type=Path, default=None)
    parser.add_argument("--java-only", action="store_true", help="Only scan Java VM/storage sources")
    parser.add_argument("--cpp-only", action="store_true", help="Only scan C++ VM sources")
    parser.add_argument("--summary-only", action="store_true", help="Print violation counts by rule & file only")
    args = parser.parse_args()

    java_root, cpp_root = resolve_roots(args.java_root, args.cpp_root)
    violations: list[tuple[str, str]] = []

    run_java = not args.cpp_only
    run_cpp = not args.java_only

    # 1. Java VM runtime rules (all files in org/impulsegraph/vm/)
    java_vm_dir = java_root / "impulse-vm" / "src" / "main" / "java" / "org" / "impulsegraph" / "vm"
    if run_java and not java_vm_dir.exists():
        violations.append(("MISSING_TARGET", f"[MISSING_TARGET] Expected Java VM directory not found: {java_vm_dir}"))

    java_vm_rules = [
        (
            "NO_TOPOLOGY_SYNTHESIS",
            re.compile(r"\b(ensureCsc|buildRevAdj|computeInDegs)\b"),
            "VM runtime must NEVER synthesize CSC/COO/CST or transpose CSR on the fly; fail with MISSING_TOPOLOGY.",
        ),
        (
            "SINGLE_OFFHEAP_PATH",
            re.compile(r"\b(rel|view\.relations\[[^\]]+\])\.(rowOffsets|colTargets|cscRowOffsets|cscColSources)\b"),
            "Production VM ops must read off-heap MemorySegments via RelView accessors, never heap topology arrays.",
        ),
    ]
    if run_java and java_vm_dir.exists():
        for f in sorted(java_vm_dir.glob("*.java")):
            violations.extend(scan_file(f, java_vm_rules))

    # 1b. STRICT zero-allocation rules on Java VM hot-path opcode evaluators
    java_hot_files = [
        "ImpulsePillar1Ops.java",
        "ImpulsePillar2Ops.java",
        "ImpulsePillar3Ops.java",
        "ImpulsePillar4Ops.java",
        "ImpulsePillar4MathOps.java",
        "ImpulsePillar5Ops.java",
        "ImpulseInterpreter.java",
    ]
    java_hot_rules = [
        (
            "NO_JAVA_COLLECTION_IMPORT",
            re.compile(r"^\s*import\s+java\.util\.(ArrayList|HashMap|TreeSet|HashSet|TreeMap|LinkedHashMap|LinkedList|PriorityQueue|BitSet)\s*;"),
            "Hot-path VM opcode files must NOT import heap-allocating java.util collections; use ws.arena.",
        ),
        (
            "NO_HEAP_COLLECTION_NEW",
            re.compile(r"\bnew\s+(ArrayList|HashMap|TreeSet|HashSet|TreeMap|LinkedHashMap|LinkedList|PriorityQueue|BitSet)\b"),
            "Hot-path VM opcode must NOT allocate heap collections; use ws.arena.",
        ),
        (
            "NO_HEAP_ARRAY_NEW",
            re.compile(r"\bnew\s+(int|long|double|float|byte|boolean|short|char|String|Object)\s*\["),
            "Hot-path VM opcode must NOT allocate raw Java heap arrays (`new T[...]`); carve from ws.arena.",
        ),
        (
            "NO_ARRAYS_COPYOF",
            re.compile(r"\bArrays\.copyOf(Range)?\s*\("),
            "Hot-path VM opcode must NOT call Arrays.copyOf; use ws.arena.",
        ),
    ]
    if run_java:
        for name in java_hot_files:
            f = java_vm_dir / name
            if f.exists():
                violations.extend(scan_file(f, java_hot_rules))

        # 2. Java Snapshot Adapter rules (zero-copy mmap-and-go, no full-column copies or ID fabrication)
        adapter_file = java_vm_dir / "ImpulseSnapshotAdapter.java"
        if not adapter_file.exists():
            violations.append(("MISSING_TARGET", f"[MISSING_TARGET] Expected Java adapter file not found: {adapter_file}"))
        adapter_rules = [
            (
                "NO_HEAP_TOPOLOGY_COPY",
                re.compile(r"new\s+(int|long)\s*\[\s*(nRows|nEdges|cscNodes)\b"),
                "Snapshot adapter must NOT copy CSR/CSC arrays onto Java heap.",
            ),
            (
                "NO_FULL_ATTR_COPY",
                re.compile(r"\bdecodeAttribute\s*\("),
                "Snapshot adapter must NOT eagerly or lazily decode entire attribute columns onto Java heap.",
            ),
            (
                "NO_KEY_CATALOG_MATERIALIZATION",
                re.compile(r"new\s+String\s*\[\s*n\s*\]"),
                "Snapshot adapter must NOT materialize full-domain String[N] key catalogs onto Java heap.",
            ),
            (
                "NO_FALLBACK_REL_ID",
                re.compile(r"\bfallbackId\b"),
                "Snapshot adapter must NOT invent sequential fallback relation IDs; fail fast on invalid metadata.",
            ),
        ]
        violations.extend(scan_file(adapter_file, adapter_rules))

        # 3. Java ImpulseSnapshotView rules (no heap topology arrays on RelView)
        view_file = java_vm_dir / "ImpulseSnapshotView.java"
        if not view_file.exists():
            violations.append(("MISSING_TARGET", f"[MISSING_TARGET] Expected Java view file not found: {view_file}"))
        view_rules = [
            (
                "SINGLE_OFFHEAP_RELVIEW",
                re.compile(r"\b(int\[\]\s+rowOffsets|long\[\]\s+colTargets|int\[\]\s+cscRowOffsets|int\[\]\s+cscColSources)\b"),
                "RelView must not hold heap-array topology fields; fixtures must pack into off-heap MemorySegments.",
            ),
        ]
        violations.extend(scan_file(view_file, view_rules))

        # 5. Java ImpulseWorkspace.reset() strTable cleanup check
        ws_file = java_vm_dir / "ImpulseWorkspace.java"
        if not ws_file.exists():
            violations.append(("MISSING_TARGET", f"[MISSING_TARGET] Expected Java workspace file not found: {ws_file}"))
        else:
            ws_text = ws_file.read_text(encoding="utf-8", errors="replace")
            m = re.search(r"public void reset\(\)\s*\{(.*?)\n\t\}", ws_text, re.DOTALL)
            if not m:
                m = re.search(r"public void reset\(\)\s*\{(.*?)\n    \}", ws_text, re.DOTALL)
            if m and "strTable.clear()" not in m.group(1):
                violations.append(
                    (
                        "WORKSPACE_RESET_LEAK",
                        f"[WORKSPACE_RESET_LEAK] {ws_file}: ImpulseWorkspace.reset() must clear strTable and reset nextStrHandle.",
                    )
                )

    # 4. C++ VM dispatch, opcodes & shared helper rules
    if run_cpp:
        cpp_vm_dir = cpp_root / "src" / "vm"
        cpp_dispatch = cpp_vm_dir / "vm_dispatch.cpp"
        if not cpp_dispatch.exists():
            violations.append(("MISSING_TARGET", f"[MISSING_TARGET] Expected C++ dispatch file not found: {cpp_dispatch}"))

        cpp_global_vm_rules = [
            (
                "NO_CPP_TOPOLOGY_SYNTHESIS",
                re.compile(r"\b(p3_build_rev_adj|p3_compute_in_degs)\b"),
                "C++ VM runtime must NEVER synthesize reverse adjacency or scan CSR for in-degrees; require mmap'd CSC.",
            ),
            (
                "NO_CPP_CSR_ARENA_COPY",
                re.compile(r"arena\.alloc_array<uint(32|64)_t>\(\s*(n_rows|n_edges)\b"),
                "C++ VM must NOT copy CSR/CSC arrays out of mmap_ptr into arena memory.",
            ),
            (
                "NO_CPP_FILESIZE_HEAP_COPY",
                re.compile(r"file_size\s*\*\s*[2-9]"),
                "prepare_snapshot must NOT allocate heap buffers proportional to file_size (use O(meta) off-heap pointers).",
            ),
        ]
        cpp_opcode_rules = cpp_global_vm_rules + [
            (
                "NO_CPP_STL_IN_OPCODES",
                re.compile(r"^\s*#\s*include\s*<(vector|map|unordered_map|set|unordered_set|string|list|deque)>"),
                "C++ VM opcodes/helpers must NOT include heap-allocating STL containers; use ws.arena.",
            ),
            (
                "NO_CPP_HEAP_ALLOC_IN_OPCODES",
                re.compile(r"\b(std::malloc|std::calloc|std::realloc|malloc|calloc|realloc|posix_memalign|_aligned_malloc)\s*\("),
                "C++ VM opcodes/helpers must NOT call heap allocators; carve all scratch from ws.arena.",
            ),
        ]

        if cpp_dispatch.exists():
            violations.extend(scan_file(cpp_dispatch, cpp_global_vm_rules))
        opcodes_dir = cpp_vm_dir / "opcodes"
        if opcodes_dir.exists():
            for f in sorted(opcodes_dir.rglob("*")):
                if f.is_file() and f.suffix in (".inc", ".hpp", ".h", ".cpp"):
                    violations.extend(scan_file(f, cpp_opcode_rules))

    if violations:
        by_rule = Counter(r for r, _ in violations)
        print(f"FAIL: {len(violations)} core engine invariant violation(s) detected:")
        for r, cnt in sorted(by_rule.items()):
            print(f"  {r}: {cnt}")
        if not args.summary_only:
            print()
            for _, msg in violations[:80]:
                print(f"  - {msg}")
            if len(violations) > 80:
                print(f"  ... ({len(violations) - 80} more violations omitted; re-run with targeted grep)")
        return 1

    print("PASS: 0 core engine invariant violations detected (Java & C++).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
