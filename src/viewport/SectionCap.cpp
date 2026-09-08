// Section cap from the triangulation. The previous implementation intersected
// the body with a half-space (BRepAlgoAPI_Common) and meshed the result: exact,
// but 735 ms on a 1683-face plate and 210 ms on a 54-face fused part, per
// recompute, and far worse on swept surfaces. Slicing the mesh the viewport
// already draws is a few milliseconds and lands on the same pixels.
#include "SectionCap.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_Triangulator.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <NCollection_List.hxx>
#include <NCollection_Vector.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <TColStd_SequenceOfInteger.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace materializr {
namespace {

// Endpoints closer than this (in mm, in the plane) are one vertex. Crossings
// of a mesh edge shared by two faces agree to floating-point noise; the finest
// mesh feature the app produces is the 0.01 mm Ultra deflection.
constexpr double kSnapMm = 1e-4;

struct Slice {
    std::vector<gp_Pnt2d> pts;                  // unique vertices, plane coords
    std::vector<std::pair<int, int>> segs;      // vertex index pairs
    std::unordered_map<int64_t, int> cells;     // snap grid cell -> vertex

    static int64_t key(int64_t ix, int64_t iy) { return (ix << 32) ^ (iy & 0xffffffffLL); }

    int vertex(const gp_Pnt2d& p) {
        const int64_t ix = static_cast<int64_t>(std::floor(p.X() / kSnapMm));
        const int64_t iy = static_cast<int64_t>(std::floor(p.Y() / kSnapMm));
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy) {
                auto it = cells.find(key(ix + dx, iy + dy));
                if (it != cells.end() && pts[it->second].Distance(p) <= kSnapMm)
                    return it->second;
            }
        pts.push_back(p);
        cells.emplace(key(ix, iy), static_cast<int>(pts.size()) - 1);
        return static_cast<int>(pts.size()) - 1;
    }
};

// Cut every triangle of every meshed face; each crossing yields one segment.
void sliceTriangulation(const TopoDS_Shape& shape, const gp_Ax3& frame, Slice& out)
{
    const gp_Pnt o = frame.Location();
    const gp_Vec n(frame.Direction()), ex(frame.XDirection()), ey(frame.YDirection());
    auto to2d = [&](const gp_Pnt& p) { gp_Vec r(o, p); return gp_Pnt2d(r.Dot(ex), r.Dot(ey)); };

    for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(fe.Current()), loc);
        if (tri.IsNull()) continue;
        const gp_Trsf trsf = loc.Transformation();
        const bool moved = loc.IsIdentity() == Standard_False;
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int idx[3];
            tri->Triangle(t).Get(idx[0], idx[1], idx[2]);
            gp_Pnt p[3];
            double d[3];
            bool pos[3];
            int nPos = 0;
            for (int k = 0; k < 3; ++k) {
                p[k] = tri->Node(idx[k]);
                if (moved) p[k].Transform(trsf);
                d[k] = gp_Vec(o, p[k]).Dot(n);
                pos[k] = d[k] >= 0.0; // on the plane counts as the discarded side
                nPos += pos[k] ? 1 : 0;
            }
            if (nPos == 0 || nPos == 3) continue;
            // The vertex alone on its side; the crossing points lie on its two edges.
            int a = 0;
            for (int k = 0; k < 3; ++k)
                if ((nPos == 1) == pos[k]) a = k;
            gp_Pnt2d q[2];
            int qi = 0;
            for (int k = 0; k < 3; ++k) {
                if (k == a) continue;
                const double s = d[a] / (d[a] - d[k]);
                q[qi++] = to2d(gp_Pnt(p[a].XYZ() + (p[k].XYZ() - p[a].XYZ()) * s));
            }
            if (q[0].Distance(q[1]) <= kSnapMm) continue; // degenerate sliver
            out.segs.emplace_back(out.vertex(q[0]), out.vertex(q[1]));
        }
    }
}

// Follow segments end to end. Loops that close are returned; chains that do
// not (an unmeshed face, a non-manifold junction) are dropped: they cannot be
// filled and no repair is attempted.
std::vector<std::vector<int>> closedLoops(const Slice& sl)
{
    std::vector<std::vector<int>> adj(sl.pts.size());
    for (size_t i = 0; i < sl.segs.size(); ++i) {
        adj[sl.segs[i].first].push_back(static_cast<int>(i));
        adj[sl.segs[i].second].push_back(static_cast<int>(i));
    }
    std::vector<bool> used(sl.segs.size(), false);
    std::vector<std::vector<int>> loops;
    for (size_t s0 = 0; s0 < sl.segs.size(); ++s0) {
        if (used[s0]) continue;
        used[s0] = true;
        const int start = sl.segs[s0].first;
        int cur = sl.segs[s0].second;
        std::vector<int> loop{start};
        bool closed = false;
        while (true) {
            if (cur == start) { closed = true; break; }
            loop.push_back(cur);
            int next = -1;
            for (int s : adj[cur])
                if (!used[s]) { next = s; break; }
            if (next < 0) break;
            used[next] = true;
            cur = sl.segs[next].first == cur ? sl.segs[next].second : sl.segs[next].first;
        }
        if (closed && loop.size() >= 3) loops.push_back(std::move(loop));
    }
    return loops;
}

// Drop vertices that sit on the line through their neighbours. A plane that
// crosses a densely meshed planar face picks up one segment per triangle, so a
// plain rectangle can arrive with hundreds of points; the fill wants four.
void mergeCollinear(const std::vector<gp_Pnt2d>& pts, std::vector<int>& loop)
{
    bool changed = true;
    while (changed && loop.size() > 3) {
        changed = false;
        for (size_t i = 0; i < loop.size() && loop.size() > 3; ++i) {
            const gp_Pnt2d& a = pts[loop[(i + loop.size() - 1) % loop.size()]];
            const gp_Pnt2d& b = pts[loop[i]];
            const gp_Pnt2d& c = pts[loop[(i + 1) % loop.size()]];
            const double acx = c.X() - a.X(), acy = c.Y() - a.Y();
            const double len = std::hypot(acx, acy);
            if (len <= kSnapMm) continue;
            const double dist = std::fabs(acx * (b.Y() - a.Y()) - acy * (b.X() - a.X())) / len;
            if (dist <= kSnapMm) {
                loop.erase(loop.begin() + static_cast<long>(i));
                changed = true;
                --i;
            }
        }
    }
}

double signedArea(const std::vector<gp_Pnt2d>& pts, const std::vector<int>& loop)
{
    double a = 0.0;
    for (size_t i = 0, n = loop.size(); i < n; ++i) {
        const gp_Pnt2d& p = pts[loop[i]];
        const gp_Pnt2d& q = pts[loop[(i + 1) % n]];
        a += p.X() * q.Y() - q.X() * p.Y();
    }
    return 0.5 * a;
}

bool contains(const std::vector<gp_Pnt2d>& pts, const std::vector<int>& loop, const gp_Pnt2d& t)
{
    bool in = false;
    for (size_t i = 0, n = loop.size(); i < n; ++i) {
        const gp_Pnt2d& p = pts[loop[i]];
        const gp_Pnt2d& q = pts[loop[(i + 1) % n]];
        if ((p.Y() > t.Y()) != (q.Y() > t.Y())) {
            const double x = p.X() + (t.Y() - p.Y()) * (q.X() - p.X()) / (q.Y() - p.Y());
            if (x > t.X()) in = !in;
        }
    }
    return in;
}

// Fill one material loop with its holes. BRepMesh_Triangulator is the 2D
// constrained Delaunay behind OCCT's own face mesher, fed points directly: no
// edges, vertices or wires to build, so a loop of hundreds of points costs
// microseconds where a polygon wire cost 50 us per edge. Material lies to the
// left of each wire: the outer loop runs counter-clockwise about the plane
// normal, holes clockwise. Wire indices are 0-based into the point vector and
// the triangles come back 1-based.
struct Ring {
    const std::vector<int>* loop;
    bool reverse;
};

void fillRegion(const std::vector<gp_Pnt2d>& pts, const std::vector<Ring>& rings,
                const gp_Ax3& frame, std::vector<float>& out)
{
    const gp_Pnt o = frame.Location();
    const gp_Vec ex(frame.XDirection()), ey(frame.YDirection());
    NCollection_Vector<gp_XYZ> xyz;
    NCollection_List<TColStd_SequenceOfInteger> wires;
    for (const Ring& r : rings) {
        TColStd_SequenceOfInteger wire;
        const int n = static_cast<int>(r.loop->size());
        for (int i = 0; i < n; ++i) {
            const gp_Pnt2d& p = pts[(*r.loop)[r.reverse ? n - 1 - i : i]];
            xyz.Append(o.XYZ() + ex.XYZ() * p.X() + ey.XYZ() * p.Y());
            wire.Append(xyz.Length() - 1);
        }
        wires.Append(wire);
    }
    BRepMesh_Triangulator triangulator(xyz, wires, frame.Direction());
    NCollection_List<Poly_Triangle> tris;
    if (!triangulator.Perform(tris)) return;
    for (NCollection_List<Poly_Triangle>::Iterator it(tris); it.More(); it.Next()) {
        int a = 0, b = 0, c = 0;
        it.Value().Get(a, b, c);
        for (int idx : {a, b, c}) {
            if (idx < 1 || idx > xyz.Length()) return;
            const gp_XYZ& p = xyz.Value(idx - 1);
            out.push_back(static_cast<float>(p.X()));
            out.push_back(static_cast<float>(p.Y()));
            out.push_back(static_cast<float>(p.Z()));
        }
    }
}

} // namespace

bool computeSectionCap(const TopoDS_Shape& shape, const gp_Pln& cuttingPlane,
                       std::vector<float>& outPositions)
{
    if (shape.IsNull()) return false;
    const size_t startSize = outPositions.size();
    try {
        // The body must straddle the plane; a plane tangent to a face is no cut.
        Bnd_Box bbox;
        BRepBndLib::Add(shape, bbox);
        if (bbox.IsVoid()) return false;
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const gp_Pnt loc = cuttingPlane.Location();
        const gp_Vec n(cuttingPlane.Axis().Direction());
        double dLo = 1e300, dHi = -1e300;
        for (int c = 0; c < 8; ++c) {
            const gp_Pnt corner((c & 1) ? xmax : xmin, (c & 2) ? ymax : ymin, (c & 4) ? zmax : zmin);
            const double d = gp_Vec(loc, corner).Dot(n);
            dLo = std::min(dLo, d);
            dHi = std::max(dHi, d);
        }
        const double straddleEps = 1e-6;
        if (!(dLo < -straddleEps && dHi > straddleEps)) return false;

        const gp_Ax3 frame = cuttingPlane.Position();
        Slice sl;
        sliceTriangulation(shape, frame, sl);
        if (sl.segs.empty()) return false;
        std::vector<std::vector<int>> loops = closedLoops(sl);
        if (loops.empty()) return false;
        for (auto& loop : loops) mergeCollinear(sl.pts, loop);

        // Nesting: a loop inside an even number of others bounds material, an
        // odd number a hole; each hole belongs to the smallest loop around it.
        const size_t L = loops.size();
        std::vector<double> area(L);
        std::vector<int> depth(L, 0), parent(L, -1);
        for (size_t i = 0; i < L; ++i) area[i] = signedArea(sl.pts, loops[i]);
        for (size_t i = 0; i < L; ++i) {
            const gp_Pnt2d& p0 = sl.pts[loops[i][0]];
            const gp_Pnt2d& p1 = sl.pts[loops[i][1]];
            const gp_Pnt2d probe((p0.X() + p1.X()) * 0.5, (p0.Y() + p1.Y()) * 0.5);
            for (size_t j = 0; j < L; ++j) {
                if (i == j || !contains(sl.pts, loops[j], probe)) continue;
                ++depth[i];
                if (parent[i] < 0 || std::fabs(area[j]) < std::fabs(area[parent[i]])) parent[i] = static_cast<int>(j);
            }
        }

        // Fill each material loop with its holes.
        for (size_t i = 0; i < L; ++i) {
            if (depth[i] % 2 != 0) continue;
            std::vector<Ring> rings{{&loops[i], area[i] < 0.0}}; // outer counter-clockwise
            for (size_t h = 0; h < L; ++h)
                if (depth[h] == depth[i] + 1 && parent[h] == static_cast<int>(i))
                    rings.push_back({&loops[h], area[h] > 0.0}); // holes clockwise
            fillRegion(sl.pts, rings, frame, outPositions);
        }
    } catch (...) {
        outPositions.resize(startSize);
        return false;
    }
    return outPositions.size() > startSize;
}

} // namespace materializr
