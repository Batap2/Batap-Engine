"""Builds ../index.html from the JSON files in this folder.

Inputs: facts.json (written by extract.py), grid.json, reading.json, findings.json,
refs.json, plan.json, report.json. Run:  python build_page.py   (runs extract.py first)
No dependency outside the standard library. SVGs are generated here, not drawn by hand.
"""
import html
import json
import math
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "index.html"


def load(name, default=None):
    p = HERE / name
    if not p.exists():
        return default if default is not None else {}
    return json.loads(p.read_text(encoding="utf-8"))


def esc(s):
    return html.escape(str(s), quote=True)


# ------------------------------------------------------------------ palette (validated, dataviz reference)
CAT = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
STATUS = {"good": "#0ca30c", "warning": "#fab219", "serious": "#ec835a", "critical": "#d03b3b"}
SEV_COLOR = {"bloquant": STATUS["critical"], "genant": STATUS["serious"], "cosmetique": STATUS["warning"]}
SEV_LABEL = {"bloquant": "bloquant", "genant": "gênant", "cosmetique": "cosmétique"}
SEQ = ["#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#2a78d6", "#1c5cab", "#104281"]
MUTED = "#898781"
GRID = "var(--grid)"

FOLDER_ORDER = ["Shaders", "Engine/base", "Renderer/Device", "Renderer/Graph", "Renderer/Draws",
                "Renderer/Features/Shadows", "Renderer/Features/RayTracing", "Renderer/Features/Gi",
                "Renderer/Features/Exposure", "Renderer/Scene", "Renderer/",
                "Engine/Components", "Engine/Systems", "Engine/Instance", "Engine/Assets", "Engine/Terrain",
                "Engine/Spatial", "Engine/Serialization", "Engine/Importers", "Engine/Utils", "Engine/Platform",
                "Engine/", "Editor", "Game", "tools"]
FOLDER_COLOR = {
    "Shaders": CAT[3], "Engine/base": MUTED,
    "Renderer/Device": CAT[0], "Renderer/Graph": CAT[6], "Renderer/Draws": CAT[2],
    "Renderer/Features/Shadows": CAT[1], "Renderer/Features/RayTracing": CAT[1], "Renderer/Features/Gi": CAT[1],
    "Renderer/Features/Exposure": CAT[1], "Renderer/Scene": CAT[4], "Renderer/": CAT[5],
}


def folder_color(f):
    if f in FOLDER_COLOR:
        return FOLDER_COLOR[f]
    if f.startswith("Engine"):
        return "#7a7872"
    return "#4d4c48"


# ------------------------------------------------------------------ paths and links
class Ctx:
    def __init__(self, facts, grid):
        self.facts = facts
        self.grid = grid
        self.root = facts["project_root"].replace("\\", "/")
        self.folder = {f["path"]: f["folder"] for f in facts["files"]}
        self.folder.update({f["path"]: f["folder"] for f in facts["frontier_files"]})
        self.base = set(grid["base_headers"])
        self.rank_of_folder = {}
        for layer in grid["layers"]:
            for fo in layer["folders"]:
                self.rank_of_folder[fo] = layer["rank"]

    def folder_of(self, path):
        r = path.replace("engine/src/Engine/", "")
        if r in self.base or r.startswith("Platform/") and False:
            return "Engine/base"
        if path in self.folder:
            return self.folder[path]
        if path.startswith("engine/src/Engine/Shaders/"):
            return "Shaders"
        if path.startswith("engine/src/Engine/Renderer/"):
            rest = path[len("engine/src/Engine/Renderer/"):].split("/")
            if len(rest) == 1:
                return "Renderer/"
            if rest[0] == "Features":
                return "Renderer/Features/" + rest[1]
            return "Renderer/" + rest[0]
        if path.startswith("engine/src/Engine/"):
            rest = path[len("engine/src/Engine/"):].split("/")
            return "Engine/" + rest[0] if len(rest) > 1 else "Engine/"
        if path.startswith("engine/src/Editor/"):
            return "Editor"
        if path.startswith("tools/"):
            return "tools"
        return "Game"

    def rank(self, path):
        return self.rank_of_folder.get(self.folder_of(path), 7)

    def vscode(self, path, line=None):
        p = path.replace("\\", "/")
        if not re.match(r"^[A-Za-z]:/", p):
            p = self.root + "/" + p
        p = p.replace(" ", "%20")
        return f"vscode://file/{p}" + (f":{line}" if line else "")

    def link(self, path, line=None, label=None):
        label = label or (short(path) + (f":{line}" if line else ""))
        return f'<a class="src" href="{self.vscode(path, line)}" title="{esc(path)}">{esc(label)}</a>'


def short(path):
    p = path.replace("engine/src/Engine/", "").replace("engine/src/", "").replace("engine/docs/", "docs/")
    return p


def name_of(path):
    return path.split("/")[-1]


# ------------------------------------------------------------------ SVG helpers
def svg_open(w, h, cls="fig", extra=""):
    return f'<svg class="{cls}" viewBox="0 0 {w} {h}" width="100%" style="max-width:{w}px" xmlns="http://www.w3.org/2000/svg" {extra}>'


def text(x, y, s, size=11, anchor="start", fill="var(--ink)", extra="", weight=""):
    w = f' font-weight="{weight}"' if weight else ""
    return f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" fill="{fill}"{w} {extra}>{esc(s)}</text>'


def curve(x1, y1, x2, y2, stroke, width, cls="", extra="", marker=True):
    mx = (x1 + x2) / 2
    d = f"M{x1:.1f},{y1:.1f} C{mx:.1f},{y1:.1f} {mx:.1f},{y2:.1f} {x2:.1f},{y2:.1f}"
    m = ' marker-end="url(#arr)"' if marker else ""
    return f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="{width:.1f}" class="{cls}" {extra}{m}/>'


DEFS = ('<defs><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">'
        '<path d="M0,0 L10,5 L0,10 z" fill="context-stroke"/></marker>'
        '<pattern id="hatch" patternUnits="userSpaceOnUse" width="6" height="6" patternTransform="rotate(45)">'
        '<line x1="0" y1="0" x2="0" y2="6" stroke="#d03b3b" stroke-width="2"/></pattern></defs>')


# ------------------------------------------------------------------ figure 1 : layer map
def fig_layers(ctx):
    F = ctx.facts
    edges = F["includes"] + F["frontier_includes"]
    cnt = Counter()
    detail = defaultdict(list)
    for e in edges:
        a, b = ctx.folder_of(e["from"]), ctx.folder_of(e["to"])
        if a == b:
            continue
        cnt[(a, b)] += 1
        detail[(a, b)].append(e)
    folders = [f for f in FOLDER_ORDER if any(f in k for k in cnt)]
    # layout: rows by rank, columns within a rank
    by_rank = defaultdict(list)
    for f in folders:
        by_rank[ctx.rank_of_folder.get(f, 7)].append(f)
    W = 1100
    rowh = 78
    H = rowh * (len(ctx.grid["layers"])) + 30
    pos = {}
    for layer in ctx.grid["layers"]:
        r = layer["rank"]
        fs = by_rank.get(r, [])
        n = max(len(fs), 1)
        for i, f in enumerate(fs):
            x = 190 + (W - 220) * (i + 0.5) / n
            y = H - 40 - r * rowh
            pos[f] = (x, y)
    out = [svg_open(W, H, "fig layers"), DEFS]
    for layer in ctx.grid["layers"]:
        y = H - 40 - layer["rank"] * rowh
        out.append(f'<line x1="150" y1="{y}" x2="{W-10}" y2="{y}" stroke="{GRID}" stroke-dasharray="2 4"/>')
        out.append(text(10, y + 4, f'{layer["rank"]}. {layer["name"]}', 11, fill="var(--muted)"))
    up_total = 0
    for (a, b), n in sorted(cnt.items(), key=lambda kv: kv[1]):
        if a not in pos or b not in pos:
            continue
        x1, y1 = pos[a]
        x2, y2 = pos[b]
        ra, rb = ctx.rank_of_folder.get(a, 7), ctx.rank_of_folder.get(b, 7)
        upward = rb > ra
        if upward:
            up_total += n
        stroke = STATUS["critical"] if upward else "var(--edge)"
        width = 1 + math.log2(n) * 1.3
        outside = ra >= 7  # includes from the rest of the engine down into the renderer: context, lighter
        # vertical curve
        d = f"M{x1:.1f},{y1 - 14 if upward else y1 + 14:.1f} C{x1:.1f},{(y1+y2)/2:.1f} {x2:.1f},{(y1+y2)/2:.1f} {x2:.1f},{y2 + 14 if upward else y2 - 14:.1f}"
        title = f"{a} → {b} : {n} include(s)" + (" (remonte)" if upward else "")
        out.append(f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="{width:.1f}" class="edge" data-from="{esc(a)}" data-to="{esc(b)}" marker-end="url(#arr)" opacity="{0.3 if outside else 0.8}"><title>{esc(title)}</title></path>')
    for f, (x, y) in pos.items():
        n_in = sum(v for (a, b), v in cnt.items() if b == f)
        n_out = sum(v for (a, b), v in cnt.items() if a == f)
        r = ctx.rank_of_folder.get(f, 7)
        n_in_row = len(by_rank.get(r, [])) or 1
        bw = min(118, (W - 230) / n_in_row - 6)
        label = f.replace("Renderer/Features/", "F/").replace("Engine/", "E/") if f != "Renderer/" else "Renderer/"
        if label == "E/":
            label = "E/racine"
        size = 11 if len(label) <= 11 else (9.5 if len(label) <= 14 else 8.5)
        out.append(f'<g class="node" data-node="{esc(f)}"><rect x="{x-bw/2:.1f}" y="{y-14:.1f}" width="{bw:.1f}" height="28" rx="6" fill="var(--surface)" stroke="{folder_color(f)}" stroke-width="2"/>'
                   + text(x, y + 4, label, size, "middle", weight="600") + f'<title>{esc(f)} : {n_in} entrants, {n_out} sortants</title></g>')
    out.append("</svg>")
    allowed = sum(n for (a, b), n in cnt.items() if ctx.rank_of_folder.get(a, 7) == 5 and ctx.rank_of_folder.get(b, 7) == 7)
    legend = (f'<p class="cap">Un nœud par dossier, une flèche par couple de dossiers, épaisseur = log du nombre d\'includes. Rouge : l\'include remonte l\'ordre des couches ({up_total} includes, dont {allowed} autorisés : Scene/ → ECS). '
              'Les flèches qui descendent depuis le reste du moteur et l\'éditeur sont estompées : elles sont le contexte, pas l\'objet. Survol d\'un nœud : ses liens.</p>')
    return "".join(out) + legend, cnt, detail


# ------------------------------------------------------------------ figure 2 : file matrix
def fig_matrix(ctx):
    F = ctx.facts
    files = [f["path"] for f in F["files"] if not f["path"].endswith((".hlsl", ".hlsli"))]
    files.sort(key=lambda p: (FOLDER_ORDER.index(ctx.folder_of(p)) if ctx.folder_of(p) in FOLDER_ORDER else 99, p))
    idx = {p: i for i, p in enumerate(files)}
    cells = defaultdict(list)
    for e in F["includes"]:
        if e["from"] in idx and e["to"] in idx:
            cells[(idx[e["from"]], idx[e["to"]])].append(e)
    n = len(files)
    cs = 9
    left, top = 230, 230
    W, H = left + n * cs + 20, top + n * cs + 20
    out = [svg_open(W, H, "fig matrix")]
    # folder bands
    prev = None
    for i, p in enumerate(files):
        fo = ctx.folder_of(p)
        if fo != prev:
            out.append(f'<line x1="{left}" y1="{top + i*cs}" x2="{left + n*cs}" y2="{top + i*cs}" stroke="var(--muted)" stroke-width="0.8"/>')
            out.append(f'<line x1="{left + i*cs}" y1="{top}" x2="{left + i*cs}" y2="{top + n*cs}" stroke="var(--muted)" stroke-width="0.8"/>')
            prev = fo
        col = folder_color(fo)
        out.append(f'<g class="mrow" data-file="{esc(p)}">' + text(left - 4, top + i * cs + 7.5, name_of(p), 7.5, "end", fill="var(--ink)", extra=f'data-i="{i}"') + "</g>")
        out.append(f'<g class="mcol" data-file="{esc(p)}" transform="translate({left + i*cs + 7},{top - 4}) rotate(-90)">' + text(0, 0, name_of(p), 7.5, "start") + "</g>")
        out.append(f'<rect x="{left - 226}" y="{top + i*cs}" width="3" height="{cs}" fill="{col}"/>')
    for (i, j), es in cells.items():
        x, y = left + j * cs, top + i * cs
        up = ctx.rank(files[j]) > ctx.rank(files[i])
        fill = STATUS["critical"] if up else CAT[0]
        payload = esc(json.dumps([{"from": e["from"], "to": e["to"], "line": e["line"]} for e in es], ensure_ascii=False))
        out.append(f'<rect class="cell" x="{x+1}" y="{y+1}" width="{cs-2}" height="{cs-2}" fill="{fill}" data-i="{i}" data-j="{j}" data-inc="{payload}"><title>{esc(short(files[i]))} → {esc(short(files[j]))} ({len(es)})</title></rect>')
    out.append(text(left + n * cs / 2, 14, "colonne = fichier inclus", 11, "middle", fill="var(--muted)"))
    out.append(f'<text x="14" y="{top + n*cs/2}" font-size="11" fill="var(--muted)" transform="rotate(-90 14 {top + n*cs/2})" text-anchor="middle">ligne = fichier qui inclut</text>')
    out.append("</svg>")
    cap = (f'<p class="cap">{n} fichiers C++ du rendu + ShaderInterop.h, triés par dossier (bande de couleur à gauche). Une case = la ligne inclut la colonne ; rouge = l\'include remonte les couches. '
           'Clic sur une case : la liste des includes avec fichier:ligne. Survol d\'un nom : sa ligne et sa colonne.</p><div id="matrix-detail" class="detail">Cliquez une case.</div>')
    return "".join(out) + cap


# ------------------------------------------------------------------ generic layered DAG (figures 3, 5, 6)
def layered_dag(nodes, edges, title_left, title_right, W=560, colw=None, node_h=22, colors=None, edge_label=False, edge_title=None):
    """nodes: list of (id, label, column, color). edges: list of (from, to, weight, title)."""
    cols = sorted({c for _, _, c, _ in nodes})
    ncol = len(cols)
    colw = colw or (W - 40) / max(ncol, 1)
    bycol = defaultdict(list)
    for nid, label, c, col in nodes:
        bycol[c].append((nid, label, col))
    maxn = max(len(v) for v in bycol.values())
    H = 40 + maxn * (node_h + 6)
    pos = {}
    out = [svg_open(W, H, "fig dag"), DEFS]
    for ci, c in enumerate(cols):
        lst = bycol[c]
        x = 20 + ci * colw + colw / 2
        for i, (nid, label, col) in enumerate(lst):
            y = 30 + i * (node_h + 6)
            pos[nid] = (x, y)
    for a, b, w, t in edges:
        if a not in pos or b not in pos:
            continue
        x1, y1 = pos[a]
        x2, y2 = pos[b]
        left_to_right = x2 > x1
        xa = x1 + colw * 0.42 if left_to_right else x1 - colw * 0.42
        xb = x2 - colw * 0.42 if left_to_right else x2 + colw * 0.42
        width = 1 + math.log2(max(w, 1)) * 1.2
        out.append(f'<path d="M{xa:.1f},{y1:.1f} C{(xa+xb)/2:.1f},{y1:.1f} {(xa+xb)/2:.1f},{y2:.1f} {xb:.1f},{y2:.1f}" fill="none" stroke="var(--edge)" stroke-width="{width:.1f}" class="edge" data-from="{esc(a)}" data-to="{esc(b)}" marker-end="url(#arr)" opacity="0.75"><title>{esc(t)}</title></path>')
    for nid, label, c, col in nodes:
        x, y = pos[nid]
        bw = colw * 0.84
        out.append(f'<g class="node" data-node="{esc(nid)}"><rect x="{x-bw/2:.1f}" y="{y-node_h/2:.1f}" width="{bw:.1f}" height="{node_h}" rx="5" fill="var(--surface)" stroke="{col}" stroke-width="1.6"/>'
                   + text(x, y + 4, label, 10, "middle") + f"<title>{esc(label)}</title></g>")
    out.append("</svg>")
    return "".join(out)


def fig_ownership_calls(ctx, reading):
    own = reading.get("ownership", [])
    calls = [c for c in reading.get("calls", []) if c.get("phase") == "frame"]
    # ownership tree → columns by depth from Engine
    nodes = {}
    children = defaultdict(list)
    for o in own:
        children[o["owner"]].append(o)
        nodes.setdefault(o["owner"], None)
        nodes.setdefault(o["owned"], None)
    roots = [n for n in nodes if not any(o["owned"] == n for o in own)]
    depth = {}

    def walk(n, d):
        if n in depth and depth[n] <= d:
            return
        depth[n] = d
        for o in children.get(n, []):
            walk(o["owned"], d + 1)
    for r in roots:
        walk(r, 0)
    dag_nodes = [(n, n, depth.get(n, 0), CAT[0]) for n in sorted(nodes, key=lambda k: (depth.get(k, 0), k))]
    dag_edges = [(o["owner"], o["owned"], 1, f'{o["owner"]} possède {o["owned"]} ({o["how"]}, {short(o["file"])}:{o["line"]})') for o in own]
    svg1 = layered_dag(dag_nodes, dag_edges, "", "", W=620, node_h=20) if own else '<p class="cap">Aucune donnée de propriété dans reading.json.</p>'
    # calls: columns by order of first appearance (caller depth)
    cnodes = {}
    cdepth = {}
    for c in calls:
        cnodes.setdefault(c["from"], None)
        cnodes.setdefault(c["to"], None)
    callers = defaultdict(list)
    for c in calls:
        callers[c["from"]].append(c["to"])
    croots = [n for n in cnodes if not any(c["to"] == n for c in calls)]

    def cwalk(n, d, seen):
        if n in cdepth and cdepth[n] <= d:
            return
        cdepth[n] = d
        for m in callers.get(n, []):
            if m not in seen:
                cwalk(m, d + 1, seen | {n})
    for r in croots:
        cwalk(r, 0, set())
    dag_nodes2 = [(n, n.split("::")[-1] if len(n) > 26 else n, cdepth.get(n, 0), CAT[2]) for n in sorted(cnodes, key=lambda k: (cdepth.get(k, 0), k))]
    dag_edges2 = [(c["from"], c["to"], 1, f'{c["from"]} → {c["to"]} ({short(c["file"])}:{c["line"]})') for c in calls]
    svg2 = layered_dag(dag_nodes2, dag_edges2, "", "", W=620, node_h=20) if calls else '<p class="cap">Aucune donnée d\'appel dans reading.json.</p>'
    return (f'<div class="two"><div><h4>Qui possède qui ({len(own)} liens)</h4>{svg1}</div>'
            f'<div><h4>Qui appelle qui pendant une frame ({len(calls)} appels)</h4>{svg2}</div></div>'
            '<p class="cap">Liens relevés à la lecture, chacun avec son fichier:ligne (survol d\'une flèche). Colonnes = profondeur depuis la racine.</p>')


# ------------------------------------------------------------------ figure 4 : the frame
def fig_frame(ctx, reading, mode):
    passes = [p for p in reading.get("passes", []) if p.get("mode") in (mode, "both")]
    if not passes:
        return '<p class="cap">Aucune passe relevée.</p>'
    res = []
    for p in passes:
        for r in p.get("reads", []) + p.get("writes", []):
            if r not in res:
                res.append(r)
    cw, rh = 26, 16
    left, top = 170, 150
    W = left + len(passes) * cw + 20
    H = top + len(res) * rh + 20
    out = [svg_open(W, H, "fig frame")]
    for j, p in enumerate(passes):
        x = left + j * cw + cw / 2
        q = p.get("queue", "graphics")
        col = CAT[0] if q == "graphics" else (CAT[2] if q == "compute" else CAT[3])
        out.append(f'<g class="pcol" data-pass="{j}"><rect x="{left + j*cw + 1}" y="{top - 6}" width="{cw-2}" height="{len(res)*rh + 6}" fill="{col}" opacity="0.06"/>'
                   f'<text transform="translate({x+4},{top-10}) rotate(-60)" font-size="9.5" fill="var(--ink)">{esc(p["name"])}</text>'
                   f'<title>{esc(p["name"])} — {esc(p.get("declared_by",""))} ({esc(short(p["file"]))}:{p["line"]}) queue {esc(q)}{(" — " + esc(p["condition"])) if p.get("condition") else ""}</title></g>')
    for i, r in enumerate(res):
        y = top + i * rh
        out.append(text(left - 6, y + 11.5, r, 9.5, "end"))
        out.append(f'<line x1="{left}" y1="{y + rh}" x2="{left + len(passes)*cw}" y2="{y + rh}" stroke="{GRID}"/>')
        for j, p in enumerate(passes):
            x = left + j * cw
            rd, wr = r in p.get("reads", []), r in p.get("writes", [])
            if rd and wr:
                out.append(f'<rect x="{x+4}" y="{y+2}" width="{cw-8}" height="{rh-4}" rx="3" fill="{CAT[6]}"><title>{esc(p["name"])} lit et écrit {esc(r)}</title></rect>')
            elif wr:
                out.append(f'<rect x="{x+4}" y="{y+2}" width="{cw-8}" height="{rh-4}" rx="3" fill="{CAT[1]}"><title>{esc(p["name"])} écrit {esc(r)}</title></rect>')
            elif rd:
                out.append(f'<rect x="{x+4}" y="{y+2}" width="{cw-8}" height="{rh-4}" rx="3" fill="none" stroke="{CAT[0]}" stroke-width="1.5"><title>{esc(p["name"])} lit {esc(r)}</title></rect>')
    out.append("</svg>")
    legend = (f'<p class="cap">{len(passes)} passes dans l\'ordre d\'enregistrement (gauche → droite), {len(res)} ressources. '
              '<span class="sw" style="background:#eb6834"></span> écrit <span class="sw" style="border:1.5px solid #2a78d6"></span> lit '
              '<span class="sw" style="background:#6250d6"></span> lit et écrit. Fond de colonne : bleu graphics, vert compute, jaune transfer. Survol d\'un nom de passe : qui la déclare, fichier:ligne, condition.</p>')
    return "".join(out) + legend


# ------------------------------------------------------------------ figure 5 : frontier
def fig_frontier(ctx):
    F = ctx.facts
    cnt = Counter()
    for e in F["frontier_includes"]:
        cnt[(ctx.folder_of(e["from"]), short(e["to"]))] += 1
    mods = sorted({a for a, _ in cnt}, key=lambda m: FOLDER_ORDER.index(m) if m in FOLDER_ORDER else 99)
    hdrs = sorted({b for _, b in cnt}, key=lambda h: -sum(v for (a, b), v in cnt.items() if b == h))
    volk = F.get("volk_closure", {})
    nodes = [(m, m, 0, "#7a7872") for m in mods]
    for h in hdrs:
        path = "engine/src/Engine/" + h
        pulls = volk.get(path, False)
        nodes.append((h, h, 1, STATUS["critical"] if pulls else CAT[2]))
    edges = [(a, b, n, f"{a} → {b} : {n} include(s)") for (a, b), n in cnt.items()]
    svg = layered_dag(nodes, edges, "", "", W=700, node_h=20)
    n_volk = sum(1 for h in hdrs if volk.get("engine/src/Engine/" + h, False))
    cap = (f'<p class="cap">{len(mods)} modules extérieurs → {len(hdrs)} en-têtes du rendu. Bord rouge : l\'en-tête tire volk.h transitivement ({n_volk} sur {len(hdrs)}) ; vert : sans Vulkan. Épaisseur = nombre d\'includes.</p>')
    return svg + cap


# ------------------------------------------------------------------ figure 6 : interop
def fig_interop(ctx):
    I = ctx.facts["interop"]
    nodes, edges = [], []
    cpps, hls = set(), set()
    for s in I["structs"]:
        nodes.append((s["name"], s["name"], 1, CAT[3]))
        for u in I["users"].get(s["name"], []):
            n = name_of(u["file"])
            if u["kind"] == "cpp":
                cpps.add(n)
                edges.append((n, s["name"], u["count"], f'{n} utilise {s["name"]} ({u["count"]}×, première ligne {u["line"]})'))
            else:
                hls.add(n)
                edges.append((s["name"], n, u["count"], f'{n} utilise {s["name"]} ({u["count"]}×, première ligne {u["line"]})'))
    for c in sorted(cpps):
        nodes.append((c, c, 0, CAT[0]))
    for h in sorted(hls):
        nodes.append((h, h, 2, CAT[2]))
    svg = layered_dag(nodes, edges, "", "", W=900, node_h=18)
    cap = (f'<p class="cap">{len(I["structs"])} structs de ShaderInterop.h (centre) ; à gauche les {len(cpps)} .cpp/.h C++ qui les utilisent, à droite les {len(hls)} .hlsl/.hlsli. Épaisseur = nombre d\'occurrences. Survol : les liens d\'un nœud.</p>')
    return svg + cap


# ------------------------------------------------------------------ figure 7 : sizes
def fig_sizes(ctx):
    F = ctx.facts
    files = sorted([f for f in F["files"] if "LtcTables.cpp" not in f["path"]], key=lambda f: -f["code"])[:28]
    W, rh = 760, 15
    H = 30 + len(files) * rh
    out = [svg_open(W, H, "fig bars")]
    scale = (W - 300) / 800
    xt = 230 + 600 * scale
    out.append(f'<line x1="{xt}" y1="8" x2="{xt}" y2="{H-6}" stroke="{STATUS["critical"]}" stroke-dasharray="4 3"/>' + text(xt + 3, 12, "seuil 600", 9, fill=STATUS["critical"]))
    for i, f in enumerate(files):
        y = 20 + i * rh
        w = f["code"] * scale
        col = STATUS["critical"] if f["code"] > 600 else folder_color(f["folder"])
        out.append(f'<a href="{ctx.vscode(f["path"])}">' + text(226, y + 10, short(f["path"]).replace("Renderer/", ""), 9.5, "end") + "</a>")
        out.append(f'<rect x="230" y="{y+2}" width="{w:.1f}" height="{rh-4}" rx="3" fill="{col}"><title>{esc(f["path"])} : {f["code"]} lignes de code, {f["total"]} au total</title></rect>')
        out.append(text(232 + w, y + 10, str(f["code"]), 9, fill="var(--muted)"))
    out.append("</svg>")
    funcs = []
    for p, fs in F["functions"].items():
        for fn in fs:
            if fn["lines"] > 60:
                funcs.append((fn["lines"], p, fn["name"], fn["start"]))
    funcs.sort(reverse=True)
    H2 = 30 + len(funcs) * rh
    out2 = [svg_open(W, H2, "fig bars")]
    scale2 = (W - 300) / 260
    for th, lab in ((80, "80"), (120, "120")):
        x = 230 + th * scale2
        out2.append(f'<line x1="{x}" y1="8" x2="{x}" y2="{H2-6}" stroke="{STATUS["critical"] if th == 120 else STATUS["warning"]}" stroke-dasharray="4 3"/>' + text(x + 3, 12, "seuil " + lab, 9, fill="var(--muted)"))
    for i, (l, p, n, s) in enumerate(funcs):
        y = 20 + i * rh
        w = l * scale2
        col = STATUS["critical"] if l > 120 else (STATUS["serious"] if l > 80 else folder_color(ctx.folder_of(p)))
        out2.append(f'<a href="{ctx.vscode(p, s)}">' + text(226, y + 10, f"{name_of(p)}:{n}", 9.5, "end") + "</a>")
        out2.append(f'<rect x="230" y="{y+2}" width="{w:.1f}" height="{rh-4}" rx="3" fill="{col}"><title>{esc(short(p))}:{s} {esc(n)} ≈ {l} lignes</title></rect>')
        out2.append(text(232 + w, y + 10, str(l), 9, fill="var(--muted)"))
    out2.append("</svg>")
    n_over_80 = sum(1 for f in funcs if f[0] > 80)
    n_over_120 = sum(1 for f in funcs if f[0] > 120)
    return (f'<h4>Fichiers (lignes de code, hors LtcTables.cpp qui est une table générée de 2 056 lignes)</h4>{"".join(out)}'
            f'<h4>Fonctions de plus de 60 lignes (accolades comptées, donc approximatif ; {n_over_80} au-dessus de 80, {n_over_120} au-dessus de 120)</h4>{"".join(out2)}'
            '<p class="cap">Chaque barre est un lien vscode. Rouge : au-dessus du seuil de la grille.</p>')


# ------------------------------------------------------------------ figure 8 : comparisons
def fig_today_ideal(ctx, rows):
    """rows: list of dicts {id, title, today, ideal, threshold, unit, ok}."""
    W, rh = 760, 30
    H = 30 + len(rows) * rh
    out = [svg_open(W, H, "fig dumbbell")]
    for i, r in enumerate(rows):
        y = 24 + i * rh
        out.append(text(4, y + 4, f'{r["id"]}. {r["title"]}', 10.5))
        t, ideal = r.get("today_num"), r.get("ideal_num")
        if t is None:
            out.append(text(300, y + 4, r.get("today", "?"), 10, fill="var(--muted)"))
            continue
        mx = max(t, ideal or 0, r.get("threshold_num") or 0, 1)
        sc = (W - 420) / mx
        x0 = 300
        xt, xi = x0 + t * sc, x0 + (ideal or 0) * sc
        out.append(f'<line x1="{x0}" y1="{y}" x2="{x0 + mx*sc}" y2="{y}" stroke="{GRID}"/>')
        th = r.get("threshold_num")
        if th is not None:
            out.append(f'<line x1="{x0 + th*sc:.1f}" y1="{y-9}" x2="{x0 + th*sc:.1f}" y2="{y+9}" stroke="{STATUS["critical"]}" stroke-dasharray="3 2"/>')
        out.append(f'<line x1="{xt:.1f}" y1="{y}" x2="{xi:.1f}" y2="{y}" stroke="var(--muted)" stroke-width="2"/>')
        out.append(f'<circle cx="{xt:.1f}" cy="{y}" r="6" fill="{STATUS["critical"] if not r.get("ok") else STATUS["good"]}"><title>aujourd\'hui : {t} {esc(r.get("unit",""))}</title></circle>')
        out.append(f'<circle cx="{xi:.1f}" cy="{y}" r="6" fill="var(--surface)" stroke="{STATUS["good"]}" stroke-width="2"><title>idéal : {ideal} {esc(r.get("unit",""))}</title></circle>')
        out.append(text(x0 + mx * sc + 8, y + 4, f'{t} → {ideal} {r.get("unit","")}', 10, fill="var(--muted)"))
    out.append("</svg>")
    return "".join(out) + '<p class="cap">Point plein : aujourd\'hui (rouge = au-dessus du seuil, vert = sous le seuil). Cercle vide : idéal proposé. Trait rouge pointillé : seuil de la grille. Les critères sans chiffre unique sont en texte.</p>'


def table_refs(refs, batap_rows):
    if not refs or not refs.get("engines"):
        return '<p class="cap">Pas de chiffre de référence vérifié : refs.json vide.</p>'
    out = ['<table class="refs"><thead><tr><th>Critère</th><th>Batap</th>']
    for e in refs["engines"]:
        out.append(f'<th>{esc(e["name"])}</th>')
    out.append("</tr></thead><tbody>")
    crits = sorted({it["criterion"] for e in refs["engines"] for it in e["items"]} | set(batap_rows))
    for c in crits:
        out.append(f'<tr><td>{c}</td><td>{batap_rows.get(c, "")}</td>')
        for e in refs["engines"]:
            items = [it for it in e["items"] if it["criterion"] == c]
            cell = []
            for it in items:
                if it.get("unverified"):
                    cell.append(f'<span class="muted">non vérifié : {esc(it.get("why",""))}</span>')
                else:
                    val = it.get("value", it.get("claim", ""))
                    unit = it.get("unit", "")
                    cell.append(f'<b>{esc(val)}</b> {esc(unit)} <span class="muted">{esc(it.get("metric",""))}</span> <a href="{esc(it["source_url"])}" target="_blank" rel="noopener">source</a>' + (f'<br><span class="muted">{esc(it["note"])}</span>' if it.get("note") else ""))
            out.append("<td>" + "<br>".join(cell) + "</td>")
        out.append("</tr>")
    out.append("</tbody></table>")
    fb = refs.get("frostbite", [])
    if fb:
        out.append('<h4>Frostbite FrameGraph (GDC 2017)</h4><ul>')
        for f in fb:
            out.append(f'<li>Critère {f["criterion"]} : {esc(f["claim"])} <a href="{esc(f["source_url"])}" target="_blank" rel="noopener">source{(", diapo " + str(f["slide"])) if f.get("slide") else ""}</a></li>')
        out.append("</ul>")
    out.append(f'<p class="cap">Chiffres vérifiés à la source le {esc(refs.get("checked_on",""))}. Un chiffre non vérifié est dit tel quel et n\'est pas comparé.</p>')
    return "".join(out)


# ------------------------------------------------------------------ figure 9 : scenario paths
def fig_scenario(ctx, scen):
    def column(title, steps, x0, col):
        out = [text(x0 + 110, 16, title, 11, "middle", weight="600")]
        for i, s in enumerate(steps):
            y = 30 + i * 46
            out.append(f'<a href="{ctx.vscode(s["file"], s.get("line"))}"><rect x="{x0}" y="{y}" width="220" height="34" rx="6" fill="var(--surface)" stroke="{col}" stroke-width="1.6"/>'
                       + text(x0 + 110, y + 14, short(s["file"]).replace("Renderer/", ""), 9.5, "middle")
                       + text(x0 + 110, y + 27, s.get("why", "")[:48], 8.5, "middle", fill="var(--muted)") + f'<title>{esc(s["file"])} — {esc(s.get("why",""))}</title></a>')
            if i < len(steps) - 1:
                out.append(f'<line x1="{x0+110}" y1="{y+34}" x2="{x0+110}" y2="{y+46}" stroke="var(--edge)" marker-end="url(#arr)"/>')
        return "".join(out)
    today, ideal = scen.get("today", []), scen.get("ideal", [])
    H = 40 + max(len(today), len(ideal), 1) * 46
    svg = [svg_open(520, H, "fig scen"), DEFS, column(f"Aujourd'hui : {len(today)} fichiers", today, 20, STATUS["critical"]),
           column(f"Idéal : {len(ideal)} fichiers", ideal, 280, STATUS["good"]), "</svg>"]
    return "".join(svg)


# ------------------------------------------------------------------ sections
def section_findings(ctx, findings):
    sev_order = {"bloquant": 0, "genant": 1, "cosmetique": 2}
    fs = sorted(findings, key=lambda f: (sev_order.get(f["severity"], 3), f.get("criterion", 99), f["id"]))
    folders = sorted({f.get("folder", "?") for f in fs})
    out = ['<div class="filters"><span>Gravité :</span>']
    for s in ("bloquant", "genant", "cosmetique"):
        n = sum(1 for f in fs if f["severity"] == s)
        out.append(f'<label><input type="checkbox" class="fsev" value="{s}" checked> <span class="pill" style="background:{SEV_COLOR[s]}">{SEV_LABEL[s]} ({n})</span></label>')
    out.append('<span>Dossier :</span><select id="ffolder"><option value="">tous</option>')
    for fo in folders:
        out.append(f'<option value="{esc(fo)}">{esc(fo)}</option>')
    out.append('</select><span id="fcount" class="muted"></span></div>')
    for f in fs:
        v = f.get("verdict", {})
        vlabel = {"confirme": "confirmé", "nuance": "nuancé", "refute": "réfuté"}.get(v.get("status", ""), "")
        out.append(f'<article class="finding" data-sev="{f["severity"]}" data-folder="{esc(f.get("folder","?"))}" id="{esc(f["id"])}">')
        out.append(f'<header><span class="pill" style="background:{SEV_COLOR[f["severity"]]}">{SEV_LABEL[f["severity"]]}</span> <b>{esc(f["id"])} — {esc(f["title"])}</b> '
                   f'<span class="muted">critère {f.get("criterion","?")} · {esc(f.get("folder",""))}</span>'
                   + (f' <span class="verdict v-{esc(v.get("status",""))}" title="{esc(v.get("note",""))}">{vlabel}</span>' if vlabel else "") + "</header>")
        out.append(f'<p class="figure"><b>Chiffre :</b> {esc(f.get("figure",""))}</p>')
        if f.get("evidence"):
            out.append("<ul class=\"ev\">")
            for e in f["evidence"]:
                out.append(f'<li>{ctx.link(e["file"], e.get("line"))} {esc(e.get("note",""))}</li>')
            out.append("</ul>")
        c = f.get("cost", {})
        out.append(f'<p><b>Coût :</b> {c.get("files","?")} fichier(s), ≈ {c.get("lines","?")} lignes, types : {esc(", ".join(c.get("types", [])) or "—")}. <b>Rapporte :</b> {esc(f.get("benefit",""))}</p>')
        if f.get("fix"):
            out.append(f'<p><b>Correction :</b> {esc(f["fix"])}</p>')
        if v.get("note"):
            out.append(f'<p class="vnote"><b>Relecture ({vlabel}) :</b> {esc(v["note"])}</p>')
        out.append("</article>")
    return "".join(out)


def section_good(ctx, goods):
    out = ["<ul class=\"good\">"]
    for g in goods:
        out.append(f'<li><b>{esc(g["title"])}</b> — {ctx.link(g["file"], g.get("line"))}. {esc(g.get("why",""))}</li>')
    out.append("</ul>")
    return "".join(out)


def section_plan(ctx, plan):
    out = []
    for i, s in enumerate(plan.get("steps", []), 1):
        out.append(f'<article class="step"><h4>Étape {i} — {esc(s["title"])}</h4><p>{esc(s.get("what",""))}</p>')
        out.append('<p><b>Fichiers :</b> ' + ", ".join(ctx.link(f["file"], f.get("line"), short(f["file"])) for f in s.get("files", [])) + "</p>")
        out.append(f'<p><b>Coût :</b> {esc(s.get("cost",""))}. <b>Constats réglés :</b> {esc(", ".join(s.get("fixes", [])))}.</p>')
        out.append(f'<p><b>Vérif :</b> {esc(s.get("verify",""))}</p></article>')
    if plan.get("note"):
        out.append(f'<p class="cap">{esc(plan["note"])}</p>')
    return "".join(out)


def section_grid(ctx, rows):
    out = ['<table class="grid"><thead><tr><th>#</th><th>Critère</th><th>Mesure</th><th>Seuil</th><th>Aujourd\'hui</th><th>Idéal proposé</th><th>Note</th></tr></thead><tbody>']
    for r in rows:
        ok = r.get("ok")
        mark = '<span class="pill" style="background:#0ca30c">tenu</span>' if ok else '<span class="pill" style="background:#d03b3b">non tenu</span>'
        out.append(f'<tr><td>{r["id"]}</td><td><b>{esc(r["title"])}</b></td><td>{esc(r["measure"])}</td><td>{esc(r["threshold"])}</td>'
                   f'<td>{r["today_html"]}</td><td>{esc(r.get("ideal",""))}</td><td>{mark} {esc(r.get("note",""))}</td></tr>')
    out.append("</tbody></table>")
    return "".join(out)


# ------------------------------------------------------------------ grid evaluation (mechanical parts)
def evaluate_grid(ctx, reading, report):
    F = ctx.facts
    rows = []
    manual = report.get("grid_today", {})
    for c in ctx.grid["criteria"]:
        r = dict(c)
        m = manual.get(str(c["id"]), {})
        auto = c.get("auto")
        today_num = None
        today_html = ""
        if auto == "upward_includes":
            ups = []
            for e in F["includes"]:
                ra, rb = ctx.rank(e["from"]), ctx.rank(e["to"])
                if rb > ra:
                    allowed = ra == 5 and rb == 7 and not any(k in e["to"] for k in ("Systems/", "World.h", "Engine.h", "GameModule.h"))
                    if not allowed:
                        ups.append(e)
            today_num = len(ups)
            by = Counter(ctx.folder_of(e["from"]) for e in ups)
            today_html = f'<b>{today_num}</b> includes qui remontent (' + ", ".join(f"{k} {v}" for k, v in by.most_common()) + ")"
        elif auto == "frontier_volk":
            cl = F.get("volk_closure", {})
            fr = [f["path"] for f in F["frontier_files"] if not f["path"].startswith(("Systems/", "Components/", "tools/"))]
            n = sum(1 for p in fr if cl.get(p))
            today_num = n
            today_html = f'<b>{n}</b> fichiers frontière sur {len(fr)} (hors jeu et tools) tirent volk.h via un en-tête du rendu'
        elif auto == "create_destroy_outside_device":
            n = 0
            byfile = Counter()
            for p, v in F["vulkan"].items():
                if ctx.folder_of(p) == "Renderer/Device":
                    continue
                for call in v["calls"]:
                    if re.match(r"vk(Create|Destroy|Allocate|Free)", call["name"]):
                        n += 1
                        byfile[name_of(p)] += 1
            today_num = n
            today_html = f'<b>{n}</b> sites dans {len(byfile)} fichiers (' + ", ".join(f"{k} {v}" for k, v in byfile.most_common(5)) + "…)"
        elif auto == "vkresult_ignored":
            tot = ign = 0
            for p, v in F["vulkan"].items():
                for call in v["calls"]:
                    if call["returns_result"]:
                        tot += 1
                        if not call["result_used"]:
                            ign += 1
            today_num = ign
            today_html = f'<b>{ign}</b> VkResult ignorés sur {tot} appels qui en renvoient un'
        elif auto == "root_files":
            roots = [f for f in F["files"] if ctx.folder_of(f["path"]) == "Renderer/" and name_of(f["path"]) not in ("Renderer.h", "Renderer.cpp", "EngineConfig.h")]
            today_num = len({name_of(f["path"]).rsplit(".", 1)[0] for f in roots})
            today_html = f'<b>{today_num}</b> fichiers (sans extension) à la racine hors Renderer et EngineConfig'
        elif auto == "functions_over_120":
            n120 = sum(1 for fs in F["functions"].values() for fn in fs if fn["lines"] > 120)
            n80 = sum(1 for fs in F["functions"].values() for fn in fs if fn["lines"] > 80)
            big = [f for f in F["files"] if f["code"] > 600 and "LtcTables" not in f["path"]]
            today_num = n120
            today_html = f'<b>{n120}</b> fonctions > 120 lignes, {n80} > 80 ; {len(big)} fichiers > 600 lignes de code'
        elif auto == "pass_order_files":
            files = sorted({p["file"] for p in reading.get("passes", [])})
            if files:
                today_num = len(files)
                today_html = f'<b>{today_num}</b> fichiers déclarent des passes : ' + ", ".join(name_of(f) for f in files)
        if m.get("html"):
            today_html = (today_html + "<br>" if today_html else "") + m["html"]
        if m.get("num") is not None and today_num is None:
            today_num = m["num"]
        r["today_num"] = today_num
        r["today_html"] = today_html or "à mesurer"
        r["ideal"] = m.get("ideal", "")
        r["ideal_num"] = m.get("ideal_num", c.get("threshold_value"))
        r["threshold_num"] = c.get("threshold_value")
        r["note"] = m.get("note", "")
        if m.get("ok") is not None:
            r["ok"] = m["ok"]
        elif today_num is not None and c.get("threshold_value") is not None:
            r["ok"] = today_num <= c["threshold_value"]
        else:
            r["ok"] = False
        rows.append(r)
    return rows


# ------------------------------------------------------------------ page
CSS = """
:root{--bg:#f9f9f7;--surface:#fcfcfb;--ink:#0b0b0b;--muted:#52514e;--grid:#e1e0d9;--edge:#9a9890;--accent:#2a78d6;--line:#c3c2b7}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#0d0d0d;--surface:#1a1a19;--ink:#f0efec;--muted:#c3c2b7;--grid:#2c2c2a;--edge:#6b6a66;--accent:#3987e5;--line:#383835}}
:root[data-theme="dark"]{--bg:#0d0d0d;--surface:#1a1a19;--ink:#f0efec;--muted:#c3c2b7;--grid:#2c2c2a;--edge:#6b6a66;--accent:#3987e5;--line:#383835}
*{box-sizing:border-box}
html{scroll-padding-top:64px}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,Segoe UI,Roboto,sans-serif}
nav{position:fixed;top:0;left:0;right:0;background:var(--surface);border-bottom:1px solid var(--line);display:flex;gap:4px;flex-wrap:wrap;padding:8px 16px;z-index:10;align-items:center}
nav a{color:var(--ink);text-decoration:none;padding:4px 8px;border-radius:6px;font-size:13px}
nav a:hover{background:var(--grid)}
nav .t{font-weight:700;margin-right:8px}
nav button{margin-left:auto;background:none;border:1px solid var(--line);color:var(--ink);border-radius:6px;padding:3px 8px;cursor:pointer}
main{max-width:1180px;margin:0 auto;padding:80px 16px 60px}
section{margin:0 0 48px;scroll-margin-top:70px}
h1{font-size:26px;margin:0 0 4px}
h2{font-size:21px;border-bottom:2px solid var(--line);padding-bottom:4px;margin:40px 0 12px}
h3{font-size:17px;margin:28px 0 8px}
h4{font-size:14px;margin:18px 0 6px;color:var(--muted)}
.lead{color:var(--muted);margin:0 0 16px}
table{border-collapse:collapse;width:100%;font-size:13.5px;background:var(--surface)}
th,td{border:1px solid var(--grid);padding:6px 8px;vertical-align:top;text-align:left}
th{background:var(--bg)}
.pill{display:inline-block;color:#fff;border-radius:10px;padding:0 8px;font-size:12px;font-weight:600;line-height:18px}
.muted{color:var(--muted)}
.cap{color:var(--muted);font-size:13px;margin:6px 0 0}
.sw{display:inline-block;width:12px;height:12px;border-radius:3px;vertical-align:middle;margin:0 2px 0 6px}
svg.fig{display:block;background:var(--surface);border:1px solid var(--grid);border-radius:8px;margin-top:8px;font-family:inherit}
svg.fig .node{cursor:pointer}
svg.fig.dim .node:not(.on){opacity:.18}
svg.fig.dim .edge:not(.on){opacity:.06}
svg.fig .edge.on{stroke:var(--accent)!important;opacity:1}
svg.fig.matrix .cell{cursor:pointer}
svg.fig.matrix .cell.sel{stroke:var(--ink);stroke-width:1.5}
svg.fig.matrix text.hl{fill:var(--accent);font-weight:700}
.two{display:grid;grid-template-columns:1fr 1fr;gap:16px}
@media(max-width:900px){.two{grid-template-columns:1fr}}
.detail{background:var(--surface);border:1px solid var(--grid);border-radius:8px;padding:8px 12px;margin-top:8px;font-size:13px;min-height:36px}
a.src{color:var(--accent);text-decoration:none;font-family:ui-monospace,Consolas,monospace;font-size:12.5px}
a.src:hover{text-decoration:underline}
.filters{display:flex;gap:12px;align-items:center;flex-wrap:wrap;margin:8px 0 16px;font-size:13px}
.filters select{background:var(--surface);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:3px 6px}
.finding{background:var(--surface);border:1px solid var(--grid);border-left:4px solid var(--line);border-radius:8px;padding:10px 14px;margin:0 0 10px}
.finding[data-sev=bloquant]{border-left-color:#d03b3b}
.finding[data-sev=genant]{border-left-color:#ec835a}
.finding[data-sev=cosmetique]{border-left-color:#fab219}
.finding header{margin-bottom:4px}
.finding p{margin:4px 0}
.finding ul.ev{margin:4px 0 4px 18px;padding:0;font-size:13.5px}
.finding .vnote{color:var(--muted);font-size:13px}
.verdict{font-size:12px;border:1px solid var(--line);border-radius:10px;padding:0 6px}
.v-confirme{border-color:#0ca30c;color:#0ca30c}.v-nuance{border-color:#fab219;color:#b07c00}.v-refute{border-color:#d03b3b;color:#d03b3b}
ul.good li{margin:0 0 8px}
.step{background:var(--surface);border:1px solid var(--grid);border-radius:8px;padding:8px 14px;margin:0 0 10px}
.step h4{margin:4px 0 6px;color:var(--ink)}
.verdict-list{font-size:16px;background:var(--surface);border:1px solid var(--grid);border-radius:8px;padding:10px 24px}
.verdict-list li{margin:6px 0}
table.refs td{font-size:12.5px}
code{font-family:ui-monospace,Consolas,monospace;font-size:13px;background:var(--grid);padding:0 4px;border-radius:4px}
.hidden{display:none}
"""

JS = r"""
(function(){
  // theme
  const btn=document.getElementById('theme');
  btn.onclick=()=>{const r=document.documentElement;const cur=r.getAttribute('data-theme');const dark=cur?cur==='dark':matchMedia('(prefers-color-scheme: dark)').matches;r.setAttribute('data-theme',dark?'light':'dark');};
  // hover highlight on every dag/layers svg
  document.querySelectorAll('svg.fig').forEach(svg=>{
    const nodes=svg.querySelectorAll('.node');if(!nodes.length)return;
    nodes.forEach(n=>{
      n.addEventListener('mouseenter',()=>{
        const id=n.dataset.node;svg.classList.add('dim');
        const on=new Set([id]);
        svg.querySelectorAll('.edge').forEach(e=>{if(e.dataset.from===id||e.dataset.to===id){e.classList.add('on');on.add(e.dataset.from);on.add(e.dataset.to);}});
        nodes.forEach(m=>{if(on.has(m.dataset.node))m.classList.add('on');});
      });
      n.addEventListener('mouseleave',()=>{svg.classList.remove('dim');svg.querySelectorAll('.on').forEach(e=>e.classList.remove('on'));});
    });
  });
  // matrix
  const mx=document.querySelector('svg.matrix');
  if(mx){
    const detail=document.getElementById('matrix-detail');
    const root=document.body.dataset.root;
    const vs=(p,l)=>'vscode://file/'+(root+'/'+p).replace(/ /g,'%20')+':'+l;
    mx.querySelectorAll('.cell').forEach(c=>c.addEventListener('click',()=>{
      mx.querySelectorAll('.cell.sel').forEach(s=>s.classList.remove('sel'));c.classList.add('sel');
      const inc=JSON.parse(c.dataset.inc);
      detail.innerHTML='<b>'+inc.length+' include(s)</b><ul>'+inc.map(e=>'<li><a class="src" href="'+vs(e.from,e.line)+'">'+e.from.replace('engine/src/Engine/','')+':'+e.line+'</a> → '+e.to.replace('engine/src/Engine/','')+'</li>').join('')+'</ul>';
    }));
    const rows=mx.querySelectorAll('.mrow text'),cols=mx.querySelectorAll('.mcol text');
    const hl=(i,on)=>{[rows[i],cols[i]].forEach(t=>t&&t.classList.toggle('hl',on));};
    rows.forEach((t,i)=>{t.addEventListener('mouseenter',()=>hl(i,true));t.addEventListener('mouseleave',()=>hl(i,false));});
    cols.forEach((t,i)=>{t.addEventListener('mouseenter',()=>hl(i,true));t.addEventListener('mouseleave',()=>hl(i,false));});
  }
  // findings filters
  const sev=document.querySelectorAll('.fsev'),fol=document.getElementById('ffolder'),cnt=document.getElementById('fcount');
  function apply(){const s=new Set([...sev].filter(c=>c.checked).map(c=>c.value));const f=fol.value;let n=0;
    document.querySelectorAll('.finding').forEach(a=>{const ok=s.has(a.dataset.sev)&&(!f||a.dataset.folder===f);a.classList.toggle('hidden',!ok);if(ok)n++;});
    cnt.textContent=n+' constat(s) affiché(s)';}
  sev.forEach(c=>c.addEventListener('change',apply));if(fol)fol.addEventListener('change',apply);if(cnt)apply();
})();
"""


def build():
    subprocess.run([sys.executable, str(HERE / "extract.py")], check=True)
    facts = load("facts.json")
    grid = load("grid.json")
    reading = load("reading.json", {})
    findings = load("findings.json", {"findings": []}).get("findings", [])
    refs = load("refs.json", {})
    plan = load("plan.json", {})
    report = load("report.json", {})
    ctx = Ctx(facts, grid)

    fig1, folder_cnt, _ = fig_layers(ctx)
    fig2 = fig_matrix(ctx)
    fig3 = fig_ownership_calls(ctx, reading)
    fig4r = fig_frame(ctx, reading, "raster")
    fig4t = fig_frame(ctx, reading, "rt")
    fig5 = fig_frontier(ctx)
    fig6 = fig_interop(ctx)
    fig7 = fig_sizes(ctx)
    rows = evaluate_grid(ctx, reading, report)
    fig8a = fig_today_ideal(ctx, rows)
    batap_rows = {r["id"]: re.sub(r"<[^>]+>", "", r["today_html"]) for r in rows}
    fig8b = table_refs(refs, batap_rows)
    scen = report.get("scenario_postprocess", {})
    fig9 = fig_scenario(ctx, scen) if scen else '<p class="cap">Scénario non renseigné dans report.json.</p>'
    scen2 = report.get("scenario_shadows_rt", {})
    fig9b = fig_scenario(ctx, scen2) if scen2 else ""

    commit = facts["commit"]
    dirty = commit.get("project_dirty", []) + [f"engine: {d}" for d in commit.get("engine_dirty", [])]
    n_files = len(facts["files"])
    n_lines = sum(f["total"] for f in facts["files"])
    sev_counts = Counter(f["severity"] for f in findings)

    verdict = report.get("verdict", [])
    goods = report.get("good", [])
    to_measure = report.get("to_measure", [])
    glossary = report.get("glossary", [])
    extra_figs = report.get("extra_figures", [])

    doc_rows = ""
    for d in facts["docs"]:
        pct = (100 * len(d["missing"]) / d["identifiers_checked"]) if d["identifiers_checked"] else 0
        missing = ", ".join(d["missing"][:8]) + ("…" if len(d["missing"]) > 8 else "")
        doc_rows += f'<tr><td>{ctx.link(d["file"], None, short(d["file"]))}</td><td>{d["lines"]}</td><td>{d["mtime"]}</td><td>{d["identifiers_checked"]}</td><td>{len(d["missing"])} ({pct:.0f} %)</td><td class="muted">{esc(missing)}</td></tr>'
    doc_verdicts = "".join(f'<li><b>{esc(d["doc"])}</b> : {esc(d["verdict"])}</li>' for d in report.get("doc_verdicts", []))

    vocab_html = ""
    for v in reading.get("vocab", []):
        if len(v.get("meanings", [])) < 2:
            continue
        vocab_html += f'<tr><td><b>{esc(v["word"])}</b></td><td>{len(v["meanings"])}</td><td>' + "<br>".join(f'{esc(m["meaning"])} ({ctx.link(m["file"], m.get("line"))})' for m in v["meanings"]) + "</td></tr>"
    gloss_html = "".join(f'<tr><td><b>{esc(g["word"])}</b></td><td>{esc(g["meaning"])}</td><td class="muted">{esc(g.get("today",""))}</td></tr>' for g in glossary)

    errors_html = "".join(f'<li>{ctx.link(e["file"], e.get("line"))} — {esc(e["what"])} : {esc(e.get("consequence",""))}</li>' for e in reading.get("errors", []))
    dead_html = "".join(f'<li>{ctx.link(d["file"], d.get("line"))} — {esc(d["what"])}</li>' for d in reading.get("dead", []))
    res_rows = "".join(f'<tr><td>{esc(r["name"])}</td><td>{esc(r["type"])}</td><td>{esc(r["owner"])}</td><td>{esc(r.get("created",""))}</td><td>{esc(r.get("destroyed",""))}</td><td>{esc(r.get("resized",""))}</td><td>{esc(r.get("lifetime",""))}</td><td class="muted">{esc(r.get("note",""))}</td></tr>' for r in reading.get("resources", []))
    dp_html = ""
    for d in reading.get("data_paths", []):
        hops = " → ".join(f'{esc(h["struct"])} ({ctx.link(h["file"], h.get("line"), name_of(h["file"]) + ":" + str(h.get("line","")))})' for h in d.get("hops", []))
        rec = "".join(f'<li>{esc(r["what"])} : {ctx.link(r["file1"], r.get("line1"))} et {ctx.link(r["file2"], r.get("line2"))}</li>' for r in d.get("recomputed", []))
        dp_html += f'<tr><td><b>{esc(d["item"])}</b></td><td>{len(d.get("hops", []))}</td><td>{hops}</td><td>{d.get("copies_without_transform", 0)}</td><td><ul>{rec}</ul></td></tr>'

    page = f"""<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Audit rendu Batap</title><style>{CSS}</style></head>
<body data-root="{esc(ctx.root)}">
<nav><span class="t">Audit du rendu</span>
<a href="#verdict">Verdict</a><a href="#grille">Grille</a><a href="#graphiques">Graphiques</a><a href="#constats">Constats</a><a href="#bien">Ce qui est bien</a><a href="#plan">Plan</a><a href="#mesurer">À mesurer</a><a href="#annexes">Annexes</a>
<button id="theme" title="clair / sombre">◐</button></nav>
<main>
<h1>Audit d'architecture du système de rendu</h1>
<p class="lead">Moteur Batap, commit projet <code>{esc(commit["project_short"])}</code>, engine <code>{esc(commit["engine_short"])}</code>, arbre de travail audité tel quel ({len(dirty)} fichiers modifiés non commités : {esc("; ".join(dirty))}). Généré le {esc(facts["generated"])} par <code>data/build_page.py</code> ; {n_files} fichiers, {n_lines:,} lignes lues.</p>

<section id="verdict"><h2>1. Verdict</h2><ol class="verdict-list">{"".join(f"<li>{v}</li>" for v in verdict)}</ol>
<p class="cap">{sev_counts.get("bloquant",0)} constats bloquants, {sev_counts.get("genant",0)} gênants, {sev_counts.get("cosmetique",0)} cosmétiques après relecture contradictoire ; {report.get("refuted",0)} constats initiaux retirés parce que réfutés.</p></section>

<section id="grille"><h2>2. Grille notée</h2>
<p class="lead">Onze critères, chacun avec sa mesure, son seuil et la valeur mesurée sur l'arbre de travail. Les valeurs automatiques viennent de <code>facts.json</code> ; les autres de la lecture.</p>
{section_grid(ctx, rows)}</section>

<section id="graphiques"><h2>3. Graphiques</h2>
<h3 id="g1">3.1 Carte des couches</h3>{fig1}
<h3 id="g2">3.2 Matrice de dépendances fichier × fichier</h3>{fig2}
<h3 id="g3">3.3 Qui possède qui, qui appelle qui</h3>{fig3}
<h3 id="g4">3.4 La frame</h3><h4>Raster</h4>{fig4r}<h4>Ray tracing</h4>{fig4t}
<h3 id="g5">3.5 La frontière</h3>{fig5}
<h3 id="g6">3.6 ShaderInterop.h : structs et utilisateurs</h3>{fig6}
<h3 id="g7">3.7 Tailles</h3>{fig7}
<h3 id="g8">3.8 Comparaisons chiffrées</h3><h4>Aujourd'hui → idéal proposé</h4>{fig8a}<h4>Batap face aux moteurs de référence</h4>{fig8b}
<h3 id="g9">3.9 Scénario « ajouter une passe de post-process »</h3>{fig9}
{('<h4>Scénario « feature ombres + RT »</h4>' + fig9b) if fig9b else ""}
{"".join(f'<h3>{esc(x["title"])}</h3>{x["html"]}' for x in extra_figs)}
</section>

<section id="constats"><h2>4. Constats</h2>
<p class="lead">Chaque constat : fichier:ligne (lien vscode), le chiffre, la gravité, le coût de la correction et ce qu'elle rapporte ici. Les constats bloquants et gênants ont été relus par un agent qui ne les a pas écrits et qui cherchait à les réfuter ; le verdict de relecture est affiché.</p>
{section_findings(ctx, findings)}</section>

<section id="bien"><h2>5. Ce qui est bien et qu'il ne faut pas casser</h2>{section_good(ctx, goods)}</section>

<section id="plan"><h2>6. Plan de corrections proposé</h2>
<p class="lead">Proposition, à valider. Une étape = un commit, chacune avec sa vérification. Après validation, il ira dans OBJECTIF_RENDU.md.</p>
{section_plan(ctx, plan)}</section>

<section id="mesurer"><h2>7. Ce qui reste à mesurer</h2><ul>{"".join(f'<li><b>{esc(m["what"])}</b> — {esc(m["why"])}<br><code>{esc(m["command"])}</code></li>' for m in to_measure)}</ul></section>

<section id="annexes"><h2>Annexes</h2>
<h3>A. Vocabulaire constaté et glossaire proposé</h3>
<table><thead><tr><th>Mot</th><th>Sens</th><th>Où</th></tr></thead><tbody>{vocab_html}</tbody></table>
<h4>Glossaire proposé</h4><table><thead><tr><th>Mot</th><th>Un seul sens</th><th>Aujourd'hui</th></tr></thead><tbody>{gloss_html}</tbody></table>
<h3>B. Ressources GPU : propriétaire, création, destruction, resize</h3>
<table><thead><tr><th>Ressource</th><th>Type</th><th>Propriétaire</th><th>Créée</th><th>Détruite</th><th>Resize</th><th>Durée de vie</th><th>Note</th></tr></thead><tbody>{res_rows}</tbody></table>
<h3>C. Chemins de données (critère 11)</h3>
<table><thead><tr><th>Donnée</th><th>Sauts</th><th>Chemin</th><th>Copies sans transformation</th><th>Recalculs</th></tr></thead><tbody>{dp_html}</tbody></table>
<h3>D. Erreurs relevées</h3><ul>{errors_html}</ul>
<h3>E. Code mort et vestiges</h3><ul>{dead_html}</ul>
<h3>F. Doc face au code</h3>
<table><thead><tr><th>Doc</th><th>Lignes</th><th>Dernier commit</th><th>Identifiants vérifiés</th><th>Absents du code</th><th>Exemples</th></tr></thead><tbody>{doc_rows}</tbody></table>
<ul>{doc_verdicts}</ul>
<p class="cap">Identifiants = mots en <code>`code`</code> dans le .md qui ressemblent à un type, une fonction ou un fichier ; vérifiés par recherche de mot entier dans les sources. Les fragments d'enum Vulkan en majuscules comptent comme absents : c'est une borne haute.</p>
<h3>G. Méthode</h3>
<p>Faits mécaniques : <code>data/extract.py</code> (regex, pas de clang ; tailles de fonctions par comptage d'accolades, approximatif). Liens de propriété, d'appel et de ressources : relevés à la lecture, consignés dans <code>data/reading.json</code> avec fichier:ligne. Constats : <code>data/findings.json</code>, chacun relu par un agent contradicteur. Références extérieures : <code>data/refs.json</code>, chaque chiffre vérifié à sa source. Relancer <code>python data/build_page.py</code> refait cette page.</p>
</section>
</main>
<script>{JS}</script>
</body></html>"""
    OUT.write_text(page, encoding="utf-8")
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KiB)")


if __name__ == "__main__":
    build()
