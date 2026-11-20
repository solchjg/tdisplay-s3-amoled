"""A small grid maze router for this two-layer board (no external router needed).

Each net is routed pad by pad with A* over F.Cu and B.Cu on a 0.127 mm grid
(so 2.54 mm header pins sit on it and one signal track fits between two of
them), joining the net's tree so far; a layer change is a via. B.Cu costs more,
so it stays mostly ground pour. Ground is not routed: each surface-mount ground
pad gets a short stub and a via into the B.Cu pour, and both layers are poured.

Two occupancy maps, one for signal tracks (0.25 mm) and one for power tracks
(0.5 mm), keep every obstacle back by the clearance plus that class's half
width plus a margin for the grid, so the result clears the 0.2 mm rule; KiCad's
DRC (check.py --pcb) confirms it.
"""
import heapq
import math

import numpy as np
import pcbnew

G = 0.127                # grid, mm
CLR = 0.2                # clearance, mm
M = 0.05                 # margin for diagonal steps between grid points
EDGE = 0.5               # copper to board edge, mm
HALF = (0.125, 0.25)     # half track width: signal, power
VIA = (0.6, 0.8)         # via diameter: signal, power
VIA_COST = 40
B_COST = 1.8
TURN_COST = 0.8
FREE, BLOCK = -1, -2


class Router:
    def __init__(self, board, ox, oy, w, h, power, keepouts=()):
        self.b, self.ox, self.oy = board, ox, oy
        self.nx, self.ny = int(round(w / G)) + 1, int(round(h / G)) + 1
        # own[class, layer, i, j]: FREE, BLOCK or the id of the net that may use the cell
        self.own = np.full((2, 2, self.nx, self.ny), FREE, dtype=np.int32)
        self.power = power
        self.netid, self.names = {}, []
        for c in (0, 1):
            e = int(math.ceil((EDGE + HALF[c]) / G))
            for L in (0, 1):
                self.own[c, L, :e, :] = BLOCK
                self.own[c, L, -e:, :] = BLOCK
                self.own[c, L, :, :e] = BLOCK
                self.own[c, L, :, -e:] = BLOCK
        for x0, y0, x1, y1 in keepouts:     # copper-free areas, such as an isolation gap
            for L in (0, 1):
                self.mark(L, x0, y0, x1, y1, BLOCK, 0)
        self.pads, self.vias = [], []
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                self._pad(pad)
        for p in self.pads:                 # fine-pitch pads: an exit lane along each pad's axis
            self._lane(p)

    def nid(self, name):
        if name not in self.netid:
            self.netid[name] = len(self.names)
            self.names.append(name)
        return self.netid[name]

    def cls(self, name):
        return 1 if name in self.power else 0

    def to_mm(self, v):
        return pcbnew.ToMM(v.x) - self.ox, pcbnew.ToMM(v.y) - self.oy

    def cell(self, x, y):
        return int(round(x / G)), int(round(y / G))

    def mark(self, layer, x0, y0, x1, y1, net, extra, seg=None):
        """Claim, in each class map, the cells within extra + that class's half width + CLR + M."""
        for c in (0, 1):
            infl = extra + HALF[c] + CLR + M
            i0, j0 = max(0, int(math.floor((x0 - infl) / G))), max(0, int(math.floor((y0 - infl) / G)))
            i1 = min(self.nx - 1, int(math.ceil((x1 + infl) / G)))
            j1 = min(self.ny - 1, int(math.ceil((y1 + infl) / G)))
            if i0 > i1 or j0 > j1:
                continue
            xs = np.arange(i0, i1 + 1)[:, None] * G
            ys = np.arange(j0, j1 + 1)[None, :] * G
            if seg is None:
                dx = np.maximum(np.maximum(x0 - xs, xs - x1), 0)
                dy = np.maximum(np.maximum(y0 - ys, ys - y1), 0)
                d = np.hypot(dx, dy)
            else:
                (ax, ay), (bx, by) = seg
                vx, vy = bx - ax, by - ay
                L2 = vx * vx + vy * vy or 1e-12
                t = np.clip(((xs - ax) * vx + (ys - ay) * vy) / L2, 0, 1)
                d = np.hypot(xs - (ax + t * vx), ys - (ay + t * vy))
            hit = d <= infl + 1e-9
            sub = self.own[c, layer, i0:i1 + 1, j0:j1 + 1]
            other = hit & (sub != FREE) & (sub != net)
            sub[hit & (sub == FREE)] = net
            sub[other] = BLOCK

    def _pad(self, pad):
        bb = pad.GetBoundingBox()
        x0, y0 = self.to_mm(bb.GetOrigin())
        x1, y1 = self.to_mm(bb.GetEnd())
        name = pad.GetNetname()
        net = self.nid(name) if name else BLOCK
        layers = [L for L, cu in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu)) if pad.IsOnLayer(cu)]
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            layers, net = [0, 1], BLOCK
        if pad.GetShape() == pcbnew.PAD_SHAPE_CIRCLE and x1 - x0 > 0:
            r = (x1 - x0) / 2
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            for L in layers:
                self.mark(L, cx, cy, cx, cy, net, r)
        else:
            for L in layers:
                self.mark(L, x0, y0, x1, y1, net, 0)
        if name:
            cx, cy = self.to_mm(pad.GetPosition())
            self.pads.append(dict(net=name, x=cx, y=cy, layers=layers, smd=len(layers) == 1, pad=pad))

    def _lane(self, p):
        """Give a narrow SMD pad (0.5 mm pitch) its axis, out 0.6 mm past each end, in the signal
        map: a 0.25 mm track there clears the neighbouring pads, which the inflated marks hide."""
        pad = p["pad"]
        if not p["smd"]:
            return
        bb = pad.GetBoundingBox()
        x0, y0 = self.to_mm(bb.GetOrigin())
        x1, y1 = self.to_mm(bb.GetEnd())
        w, h = x1 - x0, y1 - y0
        if min(w, h) >= 0.45:
            return
        net, L = self.nid(p["net"]), p["layers"][0]
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if w >= h:
            cells = [self.cell(x, cy) for x in np.arange(x0 - 0.6, x1 + 0.6 + 1e-9, G / 2)]
        else:
            cells = [self.cell(cx, y) for y in np.arange(y0 - 0.6, y1 + 0.6 + 1e-9, G / 2)]
        for i, j in cells:
            if 0 <= i < self.nx and 0 <= j < self.ny and self.own[0, L, i, j] != net:
                self.own[0, L, i, j] = net

    # ---- search
    def astar(self, net, sources, target, tlayers, any_b=False, limit=600000):
        """sources: set of (L, i, j); target: (i, j) on one of tlayers, or any B.Cu cell if any_b."""
        name = self.names[net]
        c = self.cls(name)
        grid = [self.own[c, 0].tolist(), self.own[c, 1].tolist()]
        nx, ny = self.nx, self.ny
        k = int(math.ceil((VIA[c] / 2 - HALF[c]) / G))

        def ok(L, i, j):
            v = grid[L][i][j]
            return v == FREE or v == net

        far = (VIA[1] + CLR + 0.1) / G

        def via_ok(i, j):
            for vi, vj in self.vias:
                if abs(vi - i) < far and abs(vj - j) < far:
                    return False
            if i - k < 0 or j - k < 0 or i + k >= nx or j + k >= ny:
                return False
            for L in (0, 1):
                g = grid[L]
                for a in range(i - k, i + k + 1):
                    row = g[a]
                    for b in range(j - k, j + k + 1):
                        v = row[b]
                        if v != FREE and v != net:
                            return False
            return True

        ti, tj = target
        if any_b:
            def h(i, j):
                return 0
        else:
            def h(i, j):
                di, dj = abs(i - ti), abs(j - tj)
                return max(di, dj) + 0.414 * min(di, dj)
        openq, came, cost = [], {}, {}
        for s in sources:
            cost[s] = 0
            came[s] = None
            heapq.heappush(openq, (h(s[1], s[2]), 0, s, 0, 0))
        steps = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
                 (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
        n = 0
        while openq:
            f, g, s, pdi, pdj = heapq.heappop(openq)
            if g > cost.get(s, 1e18):
                continue
            L, i, j = s
            if (any_b and L == 1) or (not any_b and i == ti and j == tj and L in tlayers):
                path = []
                while s is not None:
                    path.append(s)
                    s = came[s]
                return path[::-1]
            n += 1
            if n > limit:
                return None
            lc = B_COST if L == 1 else 1.0
            for di, dj, w in steps:
                a, b = i + di, j + dj
                if not (0 <= a < nx and 0 <= b < ny) or not ok(L, a, b):
                    continue
                if di and dj and not (ok(L, a, j) and ok(L, i, b)):
                    continue
                step = w * lc
                if (pdi or pdj) and (pdi, pdj) != (di, dj):
                    step += TURN_COST
                ns, ng = (L, a, b), g + step
                if ng < cost.get(ns, 1e18):
                    cost[ns], came[ns] = ng, s
                    heapq.heappush(openq, (ng + h(a, b), ng, ns, di, dj))
            ns = (1 - L, i, j)
            ng = g + VIA_COST
            if ng < cost.get(ns, 1e18) and via_ok(i, j):
                cost[ns], came[ns] = ng, s
                heapq.heappush(openq, (ng + h(i, j), ng, ns, 0, 0))
        return None

    # ---- committing a path
    def commit(self, net, path):
        name = self.names[net]
        half = HALF[self.cls(name)]
        bnet = self.b.FindNet(name)
        runs, start = [], 0
        for k in range(1, len(path) + 1):
            if k < len(path) and path[k][0] == path[start][0]:
                if k - start >= 2:
                    a, b, c = path[k - 2], path[k - 1], path[k]
                    if (b[1] - a[1], b[2] - a[2]) != (c[1] - b[1], c[2] - b[2]):
                        runs.append((path[start][0], path[start:k]))
                        start = k - 1
                continue
            runs.append((path[start][0], path[start:k]))
            if k < len(path):
                self.add_via(bnet, name, path[k - 1][1], path[k - 1][2])
            start = k
        for L, cells in runs:
            if len(cells) < 2:
                continue
            (_, i0, j0), (_, i1, j1) = cells[0], cells[-1]
            a, b = (i0 * G, j0 * G), (i1 * G, j1 * G)
            t = pcbnew.PCB_TRACK(self.b)
            t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(self.ox + a[0]), pcbnew.FromMM(self.oy + a[1])))
            t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(self.ox + b[0]), pcbnew.FromMM(self.oy + b[1])))
            t.SetWidth(pcbnew.FromMM(2 * half))
            t.SetLayer(pcbnew.F_Cu if L == 0 else pcbnew.B_Cu)
            t.SetNet(bnet)
            self.b.Add(t)
            self.mark(L, min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]), net, half,
                      seg=(a, b))
        return set(path)

    def add_via(self, bnet, name, i, j):
        d = VIA[self.cls(name)]
        v = pcbnew.PCB_VIA(self.b)
        v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(self.ox + i * G), pcbnew.FromMM(self.oy + j * G)))
        v.SetWidth(pcbnew.FromMM(d))
        v.SetDrill(pcbnew.FromMM(0.4 if d > 0.6 else 0.3))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(bnet)
        self.b.Add(v)
        self.vias.append((i, j))
        net = self.nid(name)
        for L in (0, 1):
            self.mark(L, i * G, j * G, i * G, j * G, net, d / 2)

    def pad_cells(self, p):
        i, j = self.cell(p["x"], p["y"])
        return {(L, i, j) for L in p["layers"]}

    def route_net(self, name, pads=None):
        net = self.nid(name)
        pads = pads or [p for p in self.pads if p["net"] == name]
        if len(pads) < 2:
            return []
        cx = sum(p["x"] for p in pads) / len(pads)
        cy = sum(p["y"] for p in pads) / len(pads)
        pads.sort(key=lambda p: math.hypot(p["x"] - cx, p["y"] - cy))
        tree = set(self.pad_cells(pads[0]))
        todo, failed = pads[1:], []
        while todo:
            todo.sort(key=lambda p: min(math.hypot(p["x"] - i * G, p["y"] - j * G) for _, i, j in tree))
            p = todo.pop(0)
            path = self.astar(net, tree, self.cell(p["x"], p["y"]), p["layers"])
            if path is None:   # a long way round: search further before giving up
                path = self.astar(net, tree, self.cell(p["x"], p["y"]), p["layers"], limit=3000000)
            if path is None:
                failed.append(p)
                continue
            tree |= self.commit(net, path)
            tree |= self.pad_cells(p)
        return failed

    def stitch(self, name):
        """A via beside each surface-mount pad of name, into the B.Cu pour."""
        net = self.nid(name)
        failed = []
        for p in self.pads:
            if p["net"] != name or not p["smd"]:
                continue
            path = self.astar(net, self.pad_cells(p), (0, 0), [1], any_b=True, limit=100000)
            if path is None:
                failed.append(p)
            else:
                self.commit(net, path)
        return failed


def route(board, ox, oy, w, h, order, power, gnds, keepouts=()):
    """gnds: the ground nets, each with its own pour; keepouts: (x0, y0, x1, y1) rectangles, mm."""
    if isinstance(gnds, str):
        gnds = [gnds]
    r = Router(board, ox, oy, w, h, set(power) - set(gnds), keepouts)   # ground stubs are thin: the pours carry it
    # ground first: each Qwiic socket's pin 1 to its tabs, then a via by every SMD ground pad,
    # so no signal track walls one in
    failed = []
    for gnd in gnds:
        by_part = {}
        for p in r.pads:
            if p["net"] == gnd:
                by_part.setdefault(p["pad"].GetParent().GetReference(), []).append(p)
        for ref, pads in by_part.items():
            if any(p["pad"].GetNumber() == "MP" for p in pads):
                r.route_net(gnd, pads)
        failed += [f"{gnd} stitch: {p['pad'].GetParent().GetReference()}.{p['pad'].GetNumber()}"
                   for p in r.stitch(gnd)]
    for name in order:
        for p in r.route_net(name):
            failed.append(f"{name}: {p['pad'].GetParent().GetReference()}.{p['pad'].GetNumber()}")
    return failed
