"""Mechanical survey of the Batap renderer. Writes facts.json next to this file.

Everything here is regex-based (no clang available): function sizes are brace
counts and are approximate, include edges are exact, symbol usage is a word
match. Run:  python extract.py
"""
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE = HERE.parents[2]                       # engine/
PROJECT = ENGINE.parent                        # Space Fret/
SRC = ENGINE / "src" / "Engine"
EDITOR = ENGINE / "src" / "Editor"
RENDERER = SRC / "Renderer"
SHADERS = SRC / "Shaders"
TOOLS = PROJECT / "tools"
GAME_SYSTEMS = PROJECT / "Systems"
VULKAN_INC = Path("C:/VulkanSDK/1.4.357.0/Include")

CPP_EXT = {".cpp", ".h", ".hpp", ".mm", ".inl"}
HLSL_EXT = {".hlsl", ".hlsli"}


def rel(p: Path) -> str:
    try:
        return p.resolve().relative_to(PROJECT.resolve()).as_posix()
    except ValueError:
        return p.as_posix()


def git(args, cwd):
    try:
        return subprocess.check_output(["git"] + args, cwd=cwd, text=True).strip()
    except Exception as e:  # noqa: BLE001
        return f"? ({e})"


# ---------------------------------------------------------------- files
def collect_files():
    files = []
    for p in sorted(RENDERER.rglob("*")):
        if p.suffix in CPP_EXT:
            files.append(p)
    for p in sorted(SHADERS.rglob("*")):
        if p.suffix in HLSL_EXT or p.name == "ShaderInterop.h":
            files.append(p)
    return files


def all_cpp_sources():
    out = []
    for root in (SRC, EDITOR, GAME_SYSTEMS, PROJECT / "Components", TOOLS):
        if root.exists():
            for p in root.rglob("*"):
                if p.suffix in CPP_EXT and "build" not in p.parts and "ThirdParty" not in p.parts:
                    out.append(p)
    for name in ("main.cpp", "game_module.cpp", "MyGame.h", "GameComponents.h"):
        p = PROJECT / name
        if p.exists():
            out.append(p)
    return sorted(set(out))


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def count_lines(text: str):
    lines = text.splitlines()
    code = [l for l in strip_comments(text).splitlines() if l.strip()]
    return {"total": len(lines), "code": len(code)}


def folder_of(relpath: str) -> str:
    """Audit folder (layer) for a path relative to the project."""
    r = relpath
    if r.startswith("engine/src/Engine/Renderer/"):
        rest = r[len("engine/src/Engine/Renderer/"):]
        parts = rest.split("/")
        if len(parts) == 1:
            return "Renderer/"
        if parts[0] == "Features":
            return "Renderer/Features/" + parts[1]
        return "Renderer/" + parts[0]
    if r.startswith("engine/src/Engine/Shaders/"):
        return "Shaders"
    if r.startswith("engine/src/Engine/"):
        rest = r[len("engine/src/Engine/"):]
        parts = rest.split("/")
        return "Engine/" + parts[0] if len(parts) > 1 else "Engine/"
    if r.startswith("engine/src/Editor/"):
        return "Editor"
    if r.startswith("tools/"):
        return "tools"
    if r.startswith("Systems/") or r.startswith("Components/") or r.count("/") == 0:
        return "Game"
    return "other"


# ---------------------------------------------------------------- includes
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
INCLUDE_ROOTS = [SRC, EDITOR, ENGINE / "src", PROJECT, SHADERS]


def resolve_include(src_file: Path, inc: str, quoted: bool):
    cands = []
    if quoted:
        cands.append(src_file.parent / inc)
    for root in INCLUDE_ROOTS:
        cands.append(root / inc)
    for c in cands:
        if c.exists():
            return c
    return None


def classify_external(inc: str) -> str:
    low = inc.lower()
    if low in ("volk.h",) or low.startswith("vulkan") or "vk_mem_alloc" in low:
        return "vulkan"
    if low.startswith("entt") or low.startswith("imgui") or low.startswith("eigen") or low.startswith("tracy") \
            or low.startswith("stb_") or low.startswith("nlohmann") or low.startswith("dxc") or "d3d" in low \
            or low.startswith("assimp"):
        return "thirdparty"
    if low in ("windows.h",) or low.startswith("win") or low.startswith("objc") or low.startswith("cocoa"):
        return "os"
    if "/" not in inc and "." not in inc:
        return "std"
    if inc.endswith(".h") and "/" not in inc:
        return "std"  # <stdint.h> etc.
    return "unknown"


def scan_includes(files, audited):
    edges = []
    externals = []
    for f in files:
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in INCLUDE_RE.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            quoted = m.group(1) == '"'
            inc = m.group(2)
            target = resolve_include(f, inc, quoted)
            if target is not None and (target.resolve() in audited or True):
                edges.append({"from": rel(f), "to": rel(target), "line": line, "text": inc})
            else:
                externals.append({"from": rel(f), "inc": inc, "line": line, "kind": classify_external(inc)})
    return edges, externals


# ---------------------------------------------------------------- functions (approximate)
CONTROL = {"if", "for", "while", "switch", "catch", "else", "return", "do", "sizeof", "alignas", "decltype", "defined", "static_assert"}
FUNC_HEAD = re.compile(r"([A-Za-z_][\w:<>~]*)\s*\([^;{}]*\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(->\s*[\w:<>&*\s]+)?\s*\{")


def approx_functions(text: str):
    """Brace-count functions. Lambdas, macros and odd formatting make this approximate."""
    clean = strip_comments(text)
    # blank out string literals
    clean = re.sub(r'"(\\.|[^"\\])*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', clean)
    lines = clean.splitlines()
    funcs = []
    depth = 0
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        # join continuation of a signature spread over a few lines
        probe = line
        j = i
        while j + 1 < n and "{" not in probe and ";" not in probe and len(probe) < 600 and probe.strip() and not probe.strip().endswith("}"):
            j += 1
            probe += " " + lines[j]
        m = FUNC_HEAD.search(probe)
        start_ok = False
        name = None
        if m and "{" in probe:
            name = m.group(1).split("::")[-1]
            head = probe[: m.start()]
            if name not in CONTROL and "](" not in probe[: m.end()] and "[&" not in head and "[=" not in head \
                    and not re.search(r"\b(if|for|while|switch|catch)\s*\(", probe[: m.end()]) \
                    and not probe.strip().startswith("#") and "=" not in head.split("(")[0]:
                start_ok = True
        if start_ok:
            # count braces from line i until matching close
            d = 0
            opened = False
            k = i
            while k < n:
                for ch in lines[k]:
                    if ch == "{":
                        d += 1
                        opened = True
                    elif ch == "}":
                        d -= 1
                if opened and d <= 0:
                    break
                k += 1
            funcs.append({"name": name, "start": i + 1, "end": k + 1, "lines": k - i + 1})
            i = k + 1
            continue
        for ch in line:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
        i += 1
    return funcs


# ---------------------------------------------------------------- vulkan surface
VK_CALL_RE = re.compile(r"\b(vk[A-Z]\w+)\s*\(")
VK_TYPE_RE = re.compile(r"\b(Vk[A-Z]\w+|VK_[A-Z0-9_]+|VmaAllocat\w+)\b")


def vk_result_functions():
    """Functions returning VkResult, parsed from the Vulkan SDK header."""
    hdr = VULKAN_INC / "vulkan" / "vulkan_core.h"
    if not hdr.exists():
        return set(), False
    text = hdr.read_text(encoding="utf-8", errors="replace")
    names = set(re.findall(r"VKAPI_ATTR\s+VkResult\s+VKAPI_CALL\s+(vk\w+)\s*\(", text))
    names |= set(re.findall(r"typedef\s+VkResult\s+\(VKAPI_PTR\s*\*\s*PFN_(vk\w+)\)", text))
    return names, True


def scan_vulkan(files, vkres):
    out = {}
    for f in files:
        if f.suffix in HLSL_EXT:
            continue
        text = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        calls = []
        for m in VK_CALL_RE.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            name = m.group(1)
            # how is the result used? look at the text before the call on the same statement
            stmt_start = max(text.rfind(";", 0, m.start()), text.rfind("{", 0, m.start()), text.rfind("}", 0, m.start()))
            before = text[stmt_start + 1 : m.start()]
            checked = bool(re.search(r"(=|return|if\s*\(|!=|==|\bcheck\w*\(|\bVK_\w*CHECK\w*\(|throw)", before))
            calls.append({"name": name, "line": line, "returns_result": name in vkres, "result_used": checked})
        types = len(VK_TYPE_RE.findall(text))
        out[rel(f)] = {"calls": calls, "vk_tokens": types}
    return out


# ---------------------------------------------------------------- render passes
ADDPASS_RE = re.compile(r"\baddPass\s*\(\s*(?:\"([^\"]*)\"|([\w:.]+))?")


def scan_passes(files):
    passes = []
    for f in files:
        if f.suffix in HLSL_EXT:
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in ADDPASS_RE.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            passes.append({"file": rel(f), "line": line, "name": m.group(1) or m.group(2) or "?"})
    return passes


# ---------------------------------------------------------------- shader interop
def scan_interop(all_sources):
    interop = SHADERS / "ShaderInterop.h"
    text = interop.read_text(encoding="utf-8", errors="replace")
    structs = []
    for m in re.finditer(r"^\s*struct\s+(\w+)\s*(?::[^{]*)?\{", text, re.M):
        line = text.count("\n", 0, m.start()) + 1
        structs.append({"name": m.group(1), "line": line})
    consts = []
    for m in re.finditer(r"^\s*(?:static\s+)?(?:const|constexpr|#define)\s+(?:\w+\s+)?(\w+)", text, re.M):
        consts.append({"name": m.group(1), "line": text.count("\n", 0, m.start()) + 1})
    # users (C++ and HLSL)
    users = {s["name"]: [] for s in structs}
    hlsl_files = [p for p in SHADERS.rglob("*") if p.suffix in HLSL_EXT]
    for f in list(all_sources) + hlsl_files:
        if f.resolve() == interop.resolve():
            continue
        try:
            t = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        except Exception:  # noqa: BLE001
            continue
        for s in structs:
            cnt = 0
            first = None
            for m in re.finditer(r"\b" + re.escape(s["name"]) + r"\b", t):
                cnt += 1
                if first is None:
                    first = t.count("\n", 0, m.start()) + 1
            if cnt:
                users[s["name"]].append({"file": rel(f), "count": cnt, "line": first, "kind": "hlsl" if f.suffix in HLSL_EXT else "cpp"})
    # [[vk::binding(X, Set)]] declarations in HLSL, with the declared resource that follows
    bindings = []
    for f in sorted(SHADERS.rglob("*")):
        if f.suffix not in HLSL_EXT:
            continue
        t = f.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"\[\[vk::binding\(\s*(\w+)\s*,\s*(\w+)\s*\)\]\]\s*([^;]*);", t):
            decl = " ".join(m.group(3).split())
            dm = re.search(r"(\w+)\s*(\[\s*\])?\s*$", decl)
            bindings.append({"file": rel(f), "line": t.count("\n", 0, m.start()) + 1, "binding": m.group(1),
                             "set": m.group(2), "decl": decl[:80], "name": dm.group(1) if dm else "?"})
        for m in re.finditer(r"\[\[vk::push_constant\]\]\s*([^;]*);", t):
            bindings.append({"file": rel(f), "line": t.count("\n", 0, m.start()) + 1, "binding": "push",
                             "set": "push", "decl": " ".join(m.group(1).split())[:80], "name": "push"})
    # binding/set constants declared in ShaderInterop.h
    slots = []
    for m in re.finditer(r"^\s*(?:static\s+)?(?:const|constexpr)\s+(?:uint|int|unsigned|uint32_t)\s+(\w+(?:Binding|Set))\s*=\s*(\d+)", text, re.M):
        slots.append({"name": m.group(1), "value": int(m.group(2)), "line": text.count("\n", 0, m.start()) + 1})
    return structs, consts, users, bindings, slots


# ---------------------------------------------------------------- hlsl includes
def scan_hlsl_includes():
    edges = []
    for f in SHADERS.rglob("*"):
        if f.suffix not in HLSL_EXT:
            continue
        t = f.read_text(encoding="utf-8", errors="replace")
        for m in INCLUDE_RE.finditer(t):
            target = (f.parent / m.group(2))
            edges.append({"from": rel(f), "to": rel(target) if target.exists() else m.group(2), "line": t.count("\n", 0, m.start()) + 1})
    return edges


# ---------------------------------------------------------------- markers
MARK_RE = re.compile(r"\b(TODO|FIXME|HACK|XXX|WIP)\b|\b(temporary|workaround|hack)\b", re.I)


def _marker_ok(m):
    return bool(m.group(1)) and m.group(1).isupper() or bool(m.group(2))


def scan_markers(files):
    out = []
    for f in files:
        t = f.read_text(encoding="utf-8", errors="replace")
        for i, l in enumerate(t.splitlines(), 1):
            m = MARK_RE.search(l)
            if m and _marker_ok(m):
                out.append({"file": rel(f), "line": i, "text": l.strip()[:140]})
    return out


# ---------------------------------------------------------------- types per file
TYPE_RE = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?(struct|enum\s+class|enum|union)\s+(\w+)\b(?!\s*;)", re.M)


def scan_types(files):
    out = {}
    for f in files:
        if f.suffix not in (".h", ".hlsli"):
            continue
        t = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        lst = []
        for m in TYPE_RE.finditer(t):
            lst.append({"kind": m.group(1).split()[0], "name": m.group(2), "line": t.count("\n", 0, m.start()) + 1})
        out[rel(f)] = lst
    return out


# ---------------------------------------------------------------- declared symbols with no external use
DECL_RE = re.compile(r"^\s*(?:static\s+|inline\s+|constexpr\s+|virtual\s+|explicit\s+|\[\[nodiscard\]\]\s*)*[\w:<>,\s\*&]+?\s+\*?&?(\w+)\s*\([^;{]*\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(=\s*0\s*)?\s*[;{]", re.M)


def scan_unused_decls(renderer_files, all_sources):
    texts = {}
    for f in all_sources:
        try:
            texts[f] = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        except Exception:  # noqa: BLE001
            pass
    hlsl_texts = {}
    results = []
    for f in renderer_files:
        if f.suffix != ".h":
            continue
        t = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        names = set()
        for m in DECL_RE.finditer(t):
            name = m.group(1)
            if name in CONTROL or name.startswith("operator") or len(name) < 3:
                continue
            names.add((name, t.count("\n", 0, m.start()) + 1))
        stem = f.stem
        for name, line in sorted(names):
            if name == stem:  # constructors
                continue
            uses = 0
            for g, gt in texts.items():
                uses += len(re.findall(r"\b" + re.escape(name) + r"\b", gt))
                # a definition `Type::name(` is not a use
                uses -= len(re.findall(r"::\s*" + re.escape(name) + r"\s*\(", gt))
            uses -= 1  # the declaration itself
            if uses <= 0:
                results.append({"file": rel(f), "line": line, "name": name})
    return results


# ---------------------------------------------------------------- docs vs code
def scan_docs(all_sources):
    docs = []
    corpus = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in all_sources)
    corpus += "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in SHADERS.rglob("*") if p.suffix in HLSL_EXT)
    existing_files = {p.name for p in all_sources} | {p.name for p in SHADERS.rglob("*")}
    for md in sorted((ENGINE / "docs").glob("*.md")):
        t = md.read_text(encoding="utf-8", errors="replace")
        idents = set(re.findall(r"`([A-Za-z_][\w:.]*(?:\(\))?)`", t))
        missing = []
        checked = 0
        for ident in sorted(idents):
            base = ident.rstrip("()")
            if base.endswith((".cpp", ".h", ".hlsl", ".hlsli", ".md", ".py", ".bat", ".json")):
                checked += 1
                if base.split("/")[-1] not in existing_files and not (ENGINE / base).exists() and not (PROJECT / base).exists():
                    missing.append(ident)
                continue
            if "::" in base or re.search(r"[A-Z]", base) or "_" in base:
                tail = base.split("::")[-1].split(".")[-1]
                if len(tail) < 3:
                    continue
                checked += 1
                if not re.search(r"\b" + re.escape(tail) + r"\b", corpus):
                    missing.append(ident)
        docs.append({"file": rel(md), "lines": len(t.splitlines()), "identifiers_checked": checked, "missing": missing,
                     "mtime": git(["log", "-1", "--format=%cs", "--", str(md)], ENGINE)})
    return docs


# ---------------------------------------------------------------- main
def main():
    files = collect_files()
    audited = {f.resolve() for f in files}
    all_sources = all_cpp_sources()
    frontier_sources = [f for f in all_sources if f.resolve() not in audited]

    file_info = []
    functions = {}
    for f in files:
        text = f.read_text(encoding="utf-8", errors="replace")
        lc = count_lines(text)
        r = rel(f)
        info = {"path": r, "folder": folder_of(r), "ext": f.suffix, **lc,
                "abs": str(f.resolve())}
        file_info.append(info)
        if f.suffix in CPP_EXT or f.suffix in HLSL_EXT:
            functions[r] = approx_functions(text)

    edges, externals = scan_includes(files, audited)
    f_edges, f_ext = scan_includes(frontier_sources, audited)
    # keep frontier edges that point into the audited set, plus the rest for layer map
    frontier_edges = [e for e in f_edges if (PROJECT / e["to"]).resolve() in audited]

    # frontier file infos (only those that include a renderer/shader header)
    frontier_files = sorted({e["from"] for e in frontier_edges})
    frontier_info = []
    for r in frontier_files:
        p = PROJECT / r
        t = p.read_text(encoding="utf-8", errors="replace")
        frontier_info.append({"path": r, "folder": folder_of(r), **count_lines(t), "abs": str(p.resolve())})

    vkres, have_sdk = vk_result_functions()
    vulkan = scan_vulkan(files, vkres)
    passes = scan_passes(files + [TOOLS / "RenderGraphCheck.cpp"])
    structs, consts, users, bindings, slots = scan_interop(all_sources)
    hlsl_edges = scan_hlsl_includes()
    markers = scan_markers(files)
    unused = scan_unused_decls(files, all_sources)
    docs = scan_docs(all_sources)
    types = scan_types(files)

    # transitive include closure over the whole engine: which files pull volk.h in
    graph = defaultdict(set)
    volk_direct = set()
    for src_file in all_sources:
        t = src_file.read_text(encoding="utf-8", errors="replace")
        for m in INCLUDE_RE.finditer(t):
            inc = m.group(2)
            if classify_external(inc) == "vulkan" and resolve_include(src_file, inc, m.group(1) == '"') is None:
                volk_direct.add(rel(src_file))
            tgt = resolve_include(src_file, inc, m.group(1) == '"')
            if tgt is not None:
                graph[rel(src_file)].add(rel(tgt))

    def pulls_volk(start):
        seen = set()
        stack = [start]
        while stack:
            cur = stack.pop()
            if cur in volk_direct:
                return True
            for nxt in graph.get(cur, ()):
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        return False

    volk_closure = {rel(p): pulls_volk(rel(p)) for p in all_sources}

    # tools & bench
    tools = []
    for name in ("RenderGraphCheck.cpp", "ShadowCheck.cpp", "render_bench.py"):
        p = TOOLS / name
        if p.exists():
            t = p.read_text(encoding="utf-8", errors="replace")
            tests = re.findall(r"^\s*(?:static\s+)?(?:void|bool|int)\s+(test\w*|check\w*|Test\w*)\s*\(", t, re.M)
            tools.append({"path": rel(p), **count_lines(t), "tests": tests, "abs": str(p.resolve())})

    facts = {
        "generated": subprocess.check_output(["python", "-c", "import datetime;print(datetime.date.today())"], text=True).strip(),
        "commit": {"project": git(["rev-parse", "HEAD"], PROJECT), "engine": git(["rev-parse", "HEAD"], ENGINE),
                   "engine_short": git(["rev-parse", "--short", "HEAD"], ENGINE),
                   "project_short": git(["rev-parse", "--short", "HEAD"], PROJECT),
                   "engine_dirty": git(["status", "--short"], ENGINE).splitlines(),
                   "project_dirty": git(["status", "--short"], PROJECT).splitlines()},
        "project_root": str(PROJECT.resolve()),
        "files": file_info,
        "frontier_files": frontier_info,
        "functions": functions,
        "includes": edges,
        "externals": externals,
        "frontier_includes": frontier_edges,
        "frontier_externals": [e for e in f_ext if e["from"] in frontier_files],
        "vulkan": vulkan,
        "vulkan_sdk_parsed": have_sdk,
        "vk_result_function_count": len(vkres),
        "passes": passes,
        "interop": {"structs": structs, "consts": consts, "users": users, "bindings": bindings, "slots": slots},
        "hlsl_includes": hlsl_edges,
        "markers": markers,
        "unused_decls": unused,
        "docs": docs,
        "tools": tools,
        "types": types,
        "volk_direct": sorted(volk_direct),
        "volk_closure": {k: v for k, v in volk_closure.items() if v},
    }
    out = HERE / "facts.json"
    out.write_text(json.dumps(facts, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size // 1024} KiB)")
    return facts


if __name__ == "__main__":
    main()
