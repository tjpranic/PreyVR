"""Map HUD parts in the extracted DanielleHUD movie, without JPEXS.

Reads the Scaleform movie's tags directly: named placements per sprite, class
exports and ActionScript 2 bytecode. Produces metadata only; no game assets or
decompiled scripts belong in Git (extract the movie with extract_native_hud.py).

  verify   check the layout NativeHudIsolation.h relies on against the movie
  tree     named display tree under a path, e.g. _root.safe.reticle_mc
  methods  a class's methods as a flat bytecode listing (constants resolved)

See docs/HUD-PARTS-2026-10-15.md.
"""
import argparse
import hashlib
import re
import struct
import sys
import zlib
from pathlib import Path

MOVIE_SHA256 = "35974883f152e345834ca690c8f6cb280152cee0ae724280e3cc98f75fb9c9dc"
HEADER = Path(__file__).resolve().parents[2] / "include" / "preyvr" / "NativeHudIsolation.h"


class Movie:
    def __init__(self, path):
        data = path.read_bytes()
        self.sha256 = hashlib.sha256(data).hexdigest()
        if data[:3] in (b"CFX", b"CWS"):
            data = data[:8] + zlib.decompress(data[8:])
        if data[:3] not in (b"GFX", b"FWS", b"CFX", b"CWS"):
            raise SystemExit("not a SWF/GFX movie")
        self.data = data
        self.root, self.sprites, self.exports = [], {}, {}
        self.init_actions, self.root_actions = {}, []
        start = self._skip_rect(8) + 4
        self._walk(start, len(data), self.root, top=True)

    # -- tag structure -------------------------------------------------------------
    def _bits(self, p):
        state = {"p": p, "bit": 0}
        def ub(n):
            v = 0
            for _ in range(n):
                byte = self.data[state["p"]]
                v = (v << 1) | ((byte >> (7 - state["bit"])) & 1)
                state["bit"] += 1
                if state["bit"] == 8:
                    state["bit"], state["p"] = 0, state["p"] + 1
            return v
        def end():
            return state["p"] + (1 if state["bit"] else 0)
        return ub, end

    def _skip_rect(self, p):
        ub, end = self._bits(p)
        n = ub(5); ub(4 * n)
        return end()

    def _matrix(self, p):
        """End of the MATRIX at p, and its translation in twips."""
        ub, end = self._bits(p)
        def sb(n):
            v = ub(n)
            return v - (1 << n) if n and v >> (n - 1) else v
        if ub(1): n = ub(5); ub(2 * n)
        if ub(1): n = ub(5); ub(2 * n)
        n = ub(5); tx = sb(n); ty = sb(n)
        return end(), (tx, ty)

    def _skip_cxform(self, p):
        ub, end = self._bits(p)
        add, mult, n = ub(1), ub(1), ub(4)
        ub(n * 4 * (add + mult))
        return end()

    def cstr(self, p):
        e = self.data.index(b"\0", p)
        return self.data[p:e].decode("latin-1"), e + 1

    def _tags(self, p, end):
        while p < end:
            code_len = struct.unpack_from("<H", self.data, p)[0]; p += 2
            code, length = code_len >> 6, code_len & 0x3F
            if length == 0x3F:
                length = struct.unpack_from("<I", self.data, p)[0]; p += 4
            yield code, p, length
            p += length
            if code == 0:
                break

    def _place(self, p, code):
        b = self.data
        if code == 26:
            flags = b[p]; p += 1
        else:
            flags, flags2 = b[p], b[p + 1]; p += 2
        depth = struct.unpack_from("<H", b, p)[0]; p += 2
        if code == 70 and (flags2 & 0x08 or (flags2 & 0x10 and flags & 0x02)):
            _, p = self.cstr(p)
        char = moved = None
        if flags & 0x02: char = struct.unpack_from("<H", b, p)[0]; p += 2
        if flags & 0x04: p, moved = self._matrix(p)
        if flags & 0x08: p = self._skip_cxform(p)
        if flags & 0x10: p += 2
        name = None
        if flags & 0x20: name, p = self.cstr(p)
        return depth, char, name, moved

    def _walk(self, start, end, into, top):
        for code, q, length in self._tags(start, end):
            if code in (26, 70):
                into.append(self._place(q, code))
            elif code == 39:
                sid = struct.unpack_from("<H", self.data, q)[0]
                self.sprites[sid] = []
                self._walk(q + 4, q + length, self.sprites[sid], top=False)
            elif code == 56:
                n = struct.unpack_from("<H", self.data, q)[0]; r = q + 2
                for _ in range(n):
                    cid = struct.unpack_from("<H", self.data, r)[0]
                    self.exports[cid], r = self.cstr(r + 2)
            elif code == 59:
                sid = struct.unpack_from("<H", self.data, q)[0]
                self.init_actions[sid] = (q + 2, q + length)
            elif code == 12 and top:
                self.root_actions.append((q, q + length))

    # -- display tree --------------------------------------------------------------
    def children(self, places):
        """Named children over the whole timeline: depth -> {names}, last character."""
        out = {}
        for depth, char, name, _ in places:
            entry = out.setdefault(depth, {"names": set(), "char": None})
            if char is not None: entry["char"] = char
            if name: entry["names"].add(name)
        return out

    def named_children(self, places):
        result = []
        for depth, entry in sorted(self.children(places).items()):
            for name in sorted(entry["names"]):
                result.append((name, entry["char"], depth))
        return result

    def resolve(self, path):
        parts = path.split(".")
        if parts[0] != "_root":
            raise SystemExit("paths start at _root")
        places = self.root
        char = None
        for part in parts[1:]:
            match = [c for n, c, _ in self.named_children(places) if n == part]
            if not match:
                return None
            char = match[0]
            places = self.sprites.get(char, [])
        return char if len(parts) > 1 else "root"

    def authored_positions(self, path):
        """Every translation (twips) the timeline places a named clip at; a placement without a matrix is (0, 0)."""
        parent, name = path.rsplit(".", 1)
        char = self.resolve(parent)
        places = self.root if char == "root" else self.sprites.get(char, [])
        return {moved or (0, 0) for _, _, placed, moved in places if placed == name}

    def subtree(self, char, depth):
        places = self.root if char == "root" else self.sprites.get(char, [])
        out = []
        for name, child, _ in self.named_children(places):
            out.append((name, child, self.subtree(child, depth - 1) if depth > 1 and child in self.sprites else []))
        return out

    # -- ActionScript 2 --------------------------------------------------------------
    OPS = {0x4E: "getMember", 0x4F: "setMember", 0x52: "callMethod", 0x3D: "callFunction",
           0x1C: "getVar", 0x1D: "setVar", 0x40: "new", 0x53: "newMethod", 0x43: "initObject",
           0x42: "initArray", 0x3E: "return", 0x17: "pop", 0x49: "equals", 0x12: "not", 0x47: "add",
           0x48: "less", 0x67: "greater", 0x3C: "defineLocal", 0x41: "declareLocal", 0x4C: "dup"}

    def listing(self, start, end, pool, want=None, inside=False, out=None, last=None):
        """Flat listing of functions whose name (the preceding push) is in `want`."""
        b, out = self.data, [] if out is None else out
        p, label = start, None
        while p < end:
            op = b[p]; p += 1
            if op < 0x80:
                if inside: out.append("  " + self.OPS.get(op, "op%02X" % op))
                continue
            length = struct.unpack_from("<H", b, p)[0]; p += 2
            q, p = p, p + length
            if op == 0x88:
                n = struct.unpack_from("<H", b, q)[0]; r = q + 2; pool[:] = []
                for _ in range(n):
                    s, r = self.cstr(r); pool.append(s)
            elif op == 0x96:
                items, r = [], q
                sizes = {1: 4, 4: 1, 5: 1, 6: 8, 7: 4}
                while r < q + length:
                    t = b[r]; r += 1
                    if t == 0: s, r = self.cstr(r); items.append(repr(s)); label = s
                    elif t in (8, 9):
                        i = b[r] if t == 8 else struct.unpack_from("<H", b, r)[0]
                        r += 1 if t == 8 else 2
                        s = pool[i] if i < len(pool) else "?"
                        items.append(repr(s)); label = s
                    elif t == 5: items.append(str(bool(b[r]))); r += 1
                    elif t == 7: items.append(str(struct.unpack_from("<i", b, r)[0])); r += 4
                    elif t == 4: items.append("r%d" % b[r]); r += 1
                    else: items.append({2: "null", 3: "undefined"}.get(t, "num")); r += sizes.get(t, 0)
                if inside: out.append("  push " + ", ".join(items))
            elif op == 0x8E:
                name, r = self.cstr(q)
                params = struct.unpack_from("<H", b, r)[0]; r += 5
                for _ in range(params):
                    r += 1; _, r = self.cstr(r)
                size = struct.unpack_from("<H", b, r)[0]
                hit = want is not None and label in want
                if inside or hit:
                    if hit and not inside: out.append(f"function {label}")
                    self.listing(p, p + size, pool, want, True, out)
                else:
                    self.listing(p, p + size, pool, want, False, out)
                p += size
            elif inside:
                out.append("  " + {0x9D: "if", 0x99: "jump", 0x87: "storeRegister"}.get(op, "op%02X" % op))
        return out

    def class_sprite(self, name):
        for cid, export in self.exports.items():
            if export == "__Packages." + name:
                return cid
        return None

    def methods(self, class_name, names):
        sid = self.class_sprite(class_name)
        if sid is None or sid not in self.init_actions:
            raise SystemExit(f"no class {class_name}")
        start, end = self.init_actions[sid]
        return self.listing(start, end, [], set(names))

    def root_script(self):
        out = []
        for start, end in self.root_actions:
            self.listing(start, end, [], None, True, out)
        return out


def header_list(name):
    text = HEADER.read_text(encoding="utf-8")
    m = re.search(r"inline constexpr std::array " + name + r"\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"{name} not found in {HEADER}")
    return re.findall(r'"([^"]+)"', m.group(1))


def verify(movie):
    failures = []
    def check(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok: failures.append(what)
    check(movie.sha256 == MOVIE_SHA256, f"movie is the patched Steam HUD ({movie.sha256[:16]}...)")
    root = ["_root." + n for n, _, _ in movie.named_children(movie.root)]
    check(root == header_list("RootChildren"), "RootChildren: _root's named clips, in draw order")
    safe = movie.resolve("_root.safe")
    safe_children = ["_root.safe." + n for n, _, _ in movie.named_children(movie.sprites[safe])]
    check(safe_children == header_list("SafeChildren"), "SafeChildren: safe's named clips, in draw order")
    unnamed = [d for d, e in movie.children(movie.sprites[safe]).items() if not e["names"]]
    check(not unnamed, "safe places nothing unnamed (a replay leaves out everything it does not keep)")
    for quadrant, listed in (("quadrant_S", "QuadrantSChildren"), ("quadrant_SE", "QuadrantSEChildren"),
                             ("quadrant_C", "QuadrantCChildren"), ("quadrant_W", "QuadrantWChildren")):
        places = movie.sprites[movie.resolve("_root.safe." + quadrant)]
        names = ["_root.safe." + quadrant + "." + n for n, _, _ in movie.named_children(places)]
        bare = [d for d, e in movie.children(places).items() if not e["names"]]
        check(names == header_list(listed) and not bare, f"{listed}: {quadrant}'s clips, nothing unnamed")
    # Run-time children: every attachMovie / createEmptyMovieClip / duplicateMovieClip
    # in a class or the root script, with the names around it.
    attached = []
    sources = [movie.root_script()] + [movie.listing(*movie.init_actions[cid], [], None, True, [])
                                       for cid, name in movie.exports.items()
                                       if name.startswith("__Packages.") and cid in movie.init_actions]
    for lines in sources:
        lines = [line.strip() for line in lines]
        for i, line in enumerate(lines):
            if re.search(r"'(attachMovie|createEmptyMovieClip|duplicateMovieClip)'", line):
                attached.append(" ".join(lines[max(0, i - 4):i + 1]))
    onto_safe = sorted(set(re.findall(r"push \d+, '(\w+)', '\w+', \d+, r\d+, 'safe'", "\n".join(attached))))
    check(["_root.safe." + n for n in onto_safe] == sorted(header_list("SafeAttached")),
          "SafeAttached: the only clips attached to safe at run time (SpiralWheel's quick_mc, assign_mc)")
    into_quadrants = [a for a in attached if re.search(r"'quadrant_(S|SE|C|W)'", a)]
    check(not into_quadrants, "nothing is attached to quadrant_S, quadrant_SE, quadrant_C or quadrant_W at run time")
    def location(cls):
        sid = movie.class_sprite("code.HUD." + cls)
        lines = " ".join(line.strip() for line in movie.listing(*movie.init_actions[sid], [], None, True, []))
        # Loc = _root.safe.<quadrant>.<clip>: "push r1, 'Loc', '_root' getVar push 'safe' getMember ..."
        return re.findall(r"'Loc', '_root' getVar push 'safe' getMember push '(\w+)' getMember push '(\w+)'", lines)
    check(("quadrant_S", "quickSelect_mc") in location("QuickSelectPC"), "QuickSelectPC (the PC hotbar) is quadrant_S.quickSelect_mc")
    check(("quadrant_SE", "quickSelect_mc") in location("QuickSelect"), "QuickSelect (gamepad) is quadrant_SE.quickSelect_mc")
    # Visibility.updatePsiArray: what opening the wheel fades out.
    hidden = " ".join(line.strip() for line in movie.methods("code.HUD.Visibility", ["updatePsiArray"]))
    faded = set(re.findall(r"push '(quadrant_\w+)' getMember push '(\w+)'", hidden))
    check(("quadrant_C", "quickLoot") in faded and ("quadrant_C", "mash_mc") in faded and
          ("quadrant_C", "interact") not in faded,
          "opening the wheel fades quadrant_C's quickLoot and mash_mc, and keeps its interact prompt")
    check(("quadrant_S", "quickSelect_mc") not in faded and ("quadrant_SE", "quickSelect_mc") not in faded,
          "and keeps both quick-select bars")
    opening = " ".join(line.strip() for line in movie.methods("code.HUD.Visibility", ["openAbilities"]))
    check("push r2, 'safe' getMember push 'quadrant_W' getMember push 'tutorial' getMember push '_visible', True "
          "setMember" in opening, "Visibility.openAbilities shows the controls box, quadrant_W.tutorial")
    script_text = " ".join(line.strip() for line in movie.root_script())
    check("push 'tut', '_root' getVar push 'safe' getMember push 'quadrant_S' getMember push 'tutHint_mc' getMember "
          "push 'inner_mc' getMember push '_root' getVar push 'safe' getMember push 'quadrant_W' getMember "
          "push 'tutorial' getMember" in script_text,
          "the root timeline's `tut` takes quadrant_W.tutorial as its controls box (ControlsPrompt)")
    update = " ".join(line.strip() for line in movie.methods("code.HUD.Tutorials.ControlsPrompt", ["tutorialUpdate"]))
    check("push r1, 'targetMC' getMember push 'help_txt' getMember" in update and
          "push r3, 'htmlText', r3, 'text' getMember setMember" in update and
          "push r2, 'vis' getMember push 'focusIsOpen' getMember" in update,
          "ControlsPrompt.tutorialUpdate writes the engine's HTML line into help_txt and checks vis.focusIsOpen")
    wheel = {n for n, _, _ in movie.subtree(movie.resolve("_root.safe.wheel_mc"), 1)}
    check({"inner_mc", "details_mc", "prompts_mc"} <= wheel, "wheel_mc holds the wheel, its details and its prompts")
    reticle = {n for n, _, _ in movie.subtree(movie.resolve("_root.safe.reticle_mc"), 1)}
    check({"reticle_master", "interact_icons"} <= reticle, "reticle_mc holds the crosshair and the interact icon")
    opened = movie.methods("code.HUD.SpiralWheel", ["wheelOpen"])
    closed = movie.methods("code.HUD.SpiralWheel", ["wheelClose"])
    check("  push r1, 'isOpen', True" in opened, "SpiralWheel.wheelOpen sets isOpen true")
    check("  push r1, 'isOpen', False" in closed, "SpiralWheel.wheelClose sets isOpen false")
    # `var spiral = new code.HUD.SpiralWheel()` at the top of the root timeline:
    # push 'spiral', ..., 'code' / getVar / 'HUD' / getMember / 'SpiralWheel' / newMethod / defineLocal.
    script = [line.strip() for line in movie.root_script()]
    for var, cls in (("spiral", "SpiralWheel"), ("spiralDetails", "SpiralWheelDetails"),
                     ("spiralPrompts", "SpiralWheelPrompts")):
        created = any(line.startswith(f"push '{var}',") and script[i + 4:i + 7] ==
                      [f"push '{cls}'", "newMethod", "defineLocal"] for i, line in enumerate(script))
        check(created, f"the root timeline defines `{var}` = new {cls} (a member of _root)")
    for cls, opener, closer in (("SpiralWheelDetails", "detailsOpen", "detailsClose"),
                                ("SpiralWheelPrompts", "itemPromptsOpen", "itemPromptsClose")):
        check("  push r1, 'isOpen', True" in movie.methods("code.HUD." + cls, [opener]) and
              "  push r1, 'isOpen', False" in movie.methods("code.HUD." + cls, [closer]),
              f"{cls}.{opener} / {closer} set and clear its isOpen")
    check(not any("detailsClose" in line or "itemPromptsClose" in line for line in closed),
          "SpiralWheel.wheelClose closes neither the details panel nor the quick-use prompts")
    details = " ".join(line.strip() for line in movie.listing(
        *movie.init_actions[movie.class_sprite("code.HUD.SpiralWheelDetails")], [], None, True, []))
    reticles = " ".join(line.strip() for line in movie.listing(
        *movie.init_actions[movie.class_sprite("code.HUD.Reticles")], [], None, True, []))
    check("push r1, 'Loc', '_root' getVar push 'safe' getMember push 'reticle_mc' getMember" in reticles,
          "Reticles.Loc is _root.safe.reticle_mc")
    for axis, method in (("_x", "reticleXOffset"), ("_y", "reticleYOffset")):
        moved = " ".join(line.strip() for line in movie.methods("code.HUD.Reticles", [method]))
        check(f"push 'Loc' getMember push '{axis}', r3 setMember" in moved and
              f"push r2, 'safe' getMember push 'quadrant_C' getMember push '{axis}', r3 setMember" in moved,
              f"Reticles.{method} moves reticle_mc.{axis} and quadrant_C.{axis} together (the prompt follows the crosshair)")
    # The world reticle's replay also puts the crosshair and interact icon back where they
    # are authored (ReticleOverrides): an examination's reticlePosition moves them inside
    # reticle_mc, by its X/Y less the last reticleXOffset/YOffset value.
    check("push r1, 'retLoc', 'code' getVar push 'HUD' getMember push 'Reticles' getMember push 'Loc' getMember "
          "push 'reticle_master' getMember setMember" in reticles and
          "push r1, 'iconLoc', 'code' getVar push 'HUD' getMember push 'Reticles' getMember push 'Loc' getMember "
          "push 'interact_icons' getMember setMember" in reticles,
          "Reticles.retLoc and iconLoc are reticle_mc.reticle_master and reticle_mc.interact_icons")
    placed = " ".join(line.strip() for line in movie.methods("code.HUD.Reticles", ["reticlePosition"]))
    check(re.findall(r"push '(\w+)' getMember push '(_[xy])', r\d+ setMember", placed) ==
          [("retLoc", "_x"), ("retLoc", "_y"), ("iconLoc", "_x"), ("iconLoc", "_y")] and
          "r1, 'centerXOff' getMember" in placed and "r1, 'centerYOff' getMember" in placed,
          "Reticles.reticlePosition moves retLoc and iconLoc, relative to the last reticleXOffset/YOffset")
    check(len(re.findall(r"push '(?:retLoc|iconLoc)' getMember push '_[xy]'", reticles)) == 4,
          "and nothing else in Reticles moves them")
    check(all(movie.authored_positions("_root.safe.reticle_mc." + n) == {(0, 0)}
              for n in ("reticle_master", "interact_icons")),
          "both are authored at (0, 0) in reticle_mc, the value ReticleOverrides puts them back to")
    camera = " ".join(line.strip() for line in movie.listing(
        *movie.init_actions[movie.class_sprite("code.HUD.Camera")], [], None, True, []))
    position = " ".join(line.strip() for line in movie.methods("code.HUD.Camera", ["HUDPos"]))
    script_lines = [line.strip() for line in movie.root_script()]
    check("push r1, 'Loc', '_root' getVar push 'safe' getMember" in camera and
          "push 'Loc' getMember push '_x', r3, r1, 'm_halfStageWidth' getMember add setMember" in position and
          "push 'Loc' getMember push '_y', r2, r1, 'm_halfStageHeight' getMember add setMember" in position and
          any(line.startswith("push 'cam',") and script_lines[i + 4:i + 7] == ["push 'Camera'", "newMethod", "defineLocal"]
              for i, line in enumerate(script_lines)),
          "Camera.HUDPos puts _root.safe at X + m_halfStageWidth, Y + m_halfStageHeight (`cam` on _root)")
    interact = " ".join(line.strip() for line in movie.listing(
        *movie.init_actions[movie.class_sprite("code.HUD.Interact")], [], None, True, []))
    formatted = " ".join(line.strip() for line in movie.methods("code.HUD.Interact", ["formatPrompts"]))
    check("'interactLoc', '_root' getVar push 'safe' getMember push 'quadrant_C' getMember push 'interact'" in interact
          and "push 'interactLoc' getMember push 'prompt', r3, 1 add add getMember" in formatted,
          "Interact.formatPrompts fills quadrant_C.interact.prompt1-3 (the engine's interactPromptArray)")
    check("'weaponsPromptMC', 'code' getVar push 'HUD' getMember push 'SpiralWheelDetails' getMember "
          "push 'detailsWeaponMC' getMember push 'prompt_mc'" in details and
          "'detailsMC', 'code' getVar push 'HUD' getMember push 'SpiralWheelDetails' getMember push 'Loc' "
          "getMember push 'details_mc'" in details,
          "wheelPrompt writes the details panel's prompt_mc (Loc.details_mc.detailsWeapon_mc.prompt_mc)")
    return not failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--movie", required=True, type=Path, help="extracted libs/ui/gfx/danielle_hud.gfx")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("verify")
    tree = sub.add_parser("tree"); tree.add_argument("path"); tree.add_argument("--depth", type=int, default=3)
    methods = sub.add_parser("methods"); methods.add_argument("cls"); methods.add_argument("names")
    args = parser.parse_args()
    movie = Movie(args.movie)
    if args.command == "verify":
        sys.exit(0 if verify(movie) else 1)
    if args.command == "tree":
        char = movie.resolve(args.path)
        if char is None:
            raise SystemExit(f"{args.path} is not placed")
        def show(nodes, prefix, indent):
            for name, child, kids in nodes:
                print("  " * indent + f"{prefix}.{name}")
                show(kids, f"{prefix}.{name}", indent + 1)
        show(movie.subtree(char, args.depth), args.path, 0)
    if args.command == "methods":
        print("\n".join(movie.methods(args.cls, args.names.split(","))))


if __name__ == "__main__":
    main()
