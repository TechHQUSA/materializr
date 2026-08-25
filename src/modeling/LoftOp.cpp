#include "LoftOp.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>
#include <Standard_ErrorHandler.hxx> // OCC_CATCH_SIGNALS
#include <BRepAlgoAPI_Cut.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BOPAlgo_CheckResult.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopoDS_Edge.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopTools_SequenceOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <cstdlib>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <cstdio>
#include <vector>
#include <imgui.h>

namespace {

// What ThruSections is actually being handed. The sections it pairs are
// COMPUTED REGIONS, not the raw sketch geometry, so nothing about them can be
// worked out from the project file alone -- a twisted loft had to be diagnosed
// from here.
//
// Winding is the number to watch: ThruSections joins wire A's parameter t to
// wire B's parameter t, so two sections whose loops run opposite ways around
// their own normals get connected front-to-back and the surface crosses itself.
gp_Vec wireWinding(const TopoDS_Wire& w) {
    std::vector<gp_Pnt> v;
    for (BRepTools_WireExplorer ex(w); ex.More(); ex.Next())
        v.push_back(BRep_Tool::Pnt(ex.CurrentVertex()));
    gp_Vec n(0, 0, 0);
    for (std::size_t i = 0; i < v.size(); ++i) {          // Newell's method
        const gp_Pnt& a = v[i];
        const gp_Pnt& b = v[(i + 1) % v.size()];
        n += gp_Vec((a.Y() - b.Y()) * (a.Z() + b.Z()),
                    (a.Z() - b.Z()) * (a.X() + b.X()),
                    (a.X() - b.X()) * (a.Y() + b.Y()));
    }
    if (n.Magnitude() > 1e-12) n.Normalize();
    return n;
}

// Write the sections to a BREP file when MATERIALIZR_DUMP_LOFT names a path.
// The profiles ThruSections receives are COMPUTED REGIONS, so they cannot be
// reconstructed from the project file -- without this, every hypothesis about a
// bad loft has to be tested by asking the user to re-run the app.
void dumpSections(const std::vector<TopoDS_Wire>& profiles) {
    const char* path = std::getenv("MATERIALIZR_DUMP_LOFT");
    if (!path || !*path) return;
    try {
        BRep_Builder b;
        TopoDS_Compound c;
        b.MakeCompound(c);
        for (const auto& w : profiles)
            if (!w.IsNull()) b.Add(c, w);
        if (BRepTools::Write(c, path))
            std::fprintf(stderr, "[Loft] sections written to %s\n", path);
    } catch (...) {}
}

// ─── Tip-split fallback for a self-intersecting loft ────────────────────────
//
// ThruSections pairs single-loop sections by arc-length parameter, which folds
// the surface whenever the loops' features sit at different perimeter
// fractions. Worst case measured (robot dog cover.mzr, two ~2 mm-wide C-shaped
// rim bands sharing the same two tips): every parameterisation-level remedy
// failed --
//
//     raw wires                          self-intersecting (vol 2856)
//     BRepFill_CompatibleWires           self-intersecting, vol NEGATIVE (-770)
//     single approximated curve/section  collapsed (vol 150-674)
//     arc-length resample + best seam    still folded (rms misfit 18.7 mm)
//
// because on a thin band the wrong run-pairing differs by only the band width,
// which no distance metric can see. What DOES work is structural: split each
// closed section into exactly TWO edges at its extreme points along the loft
// set's longest axis (the band tips), and let ThruSections' vertex matching pin
// tip to tip. Same part, this path: vol 3502, valid, no self-intersection.
//
// The fallback only runs when the normal loft came out self-intersecting, so
// well-behaved lofts keep their exact geometry and pay nothing.
bool selfIntersects(const TopoDS_Shape& s) {
    try {
        BOPAlgo_ArgumentAnalyzer an;
        an.SetShape1(s);
        an.OperationType() = BOPAlgo_UNKNOWN;
        an.SelfInterMode() = Standard_True;
        an.Perform();
        for (BOPAlgo_ListIteratorOfListOfCheckResult it(an.GetCheckResult());
             it.More(); it.Next())
            if (it.Value().GetCheckStatus() == BOPAlgo_SelfIntersect) return true;
    } catch (...) {}
    return false;
}

std::vector<gp_Pnt> densePoly(const TopoDS_Wire& w, int n) {
    std::vector<gp_Pnt> out; out.reserve(n);
    try {
        BRepAdaptor_CompCurve cc(w);
        const double L = GCPnts_AbscissaPoint::Length(cc);
        const double t0 = cc.FirstParameter();
        if (L < 1e-9) return out;
        for (int i = 0; i < n; ++i) {
            GCPnts_AbscissaPoint ap(cc, L * (double)i / n, t0);
            out.push_back(cc.Value(ap.IsDone() ? ap.Parameter() : t0));
        }
    } catch (...) { out.clear(); }
    return out;
}

TopoDS_Edge interpolatedEdge(const std::vector<gp_Pnt>& pts) {
    try {
        Handle(TColgp_HArray1OfPnt) a = new TColgp_HArray1OfPnt(1, (int)pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) a->SetValue((int)i + 1, pts[i]);
        GeomAPI_Interpolate ip(a, Standard_False, 1e-7);
        ip.Perform();
        if (!ip.IsDone()) return TopoDS_Edge();
        return BRepBuilderAPI_MakeEdge(ip.Curve()).Edge();
    } catch (...) { return TopoDS_Edge(); }
}

// Rebuild one closed loop as a 2-edge wire split at its extremes along `axis`
// (0=X 1=Y 2=Z). Runs are resampled uniformly by arc length.
TopoDS_Wire tipSplitWire(const TopoDS_Wire& w, int axis) {
    const std::vector<gp_Pnt> p = densePoly(w, 2048);
    if (p.size() < 8) return TopoDS_Wire();
    auto coord = [axis](const gp_Pnt& q) {
        return axis == 0 ? q.X() : axis == 1 ? q.Y() : q.Z();
    };
    int iMin = 0, iMax = 0;
    const int n = (int)p.size();
    for (int i = 1; i < n; ++i) {
        if (coord(p[i]) < coord(p[iMin])) iMin = i;
        if (coord(p[i]) > coord(p[iMax])) iMax = i;
    }
    if (iMin == iMax) return TopoDS_Wire();
    auto walk = [&](int a, int b) {
        std::vector<gp_Pnt> r;
        for (int i = a;; i = (i + 1) % n) { r.push_back(p[i]); if (i == b) break; }
        return r;
    };
    auto resample = [](const std::vector<gp_Pnt>& r, int k) {
        std::vector<double> cum(r.size(), 0.0);
        for (std::size_t i = 1; i < r.size(); ++i) cum[i] = cum[i-1] + r[i-1].Distance(r[i]);
        const double L = cum.back();
        std::vector<gp_Pnt> out; out.reserve(k);
        std::size_t j = 0;
        for (int i = 0; i < k; ++i) {
            const double s = L * (double)i / (k - 1);
            while (j + 1 < cum.size() && cum[j+1] < s) ++j;
            const double seg = cum[j+1] - cum[j];
            const double t = seg > 1e-12 ? (s - cum[j]) / seg : 0.0;
            out.push_back(gp_Pnt(r[j].X() + (r[j+1].X()-r[j].X())*t,
                                 r[j].Y() + (r[j+1].Y()-r[j].Y())*t,
                                 r[j].Z() + (r[j+1].Z()-r[j].Z())*t));
        }
        return out;
    };
    const TopoDS_Edge e1 = interpolatedEdge(resample(walk(iMax, iMin), 80));
    const TopoDS_Edge e2 = interpolatedEdge(resample(walk(iMin, iMax), 80));
    if (e1.IsNull() || e2.IsNull()) return TopoDS_Wire();
    try {
        BRepBuilderAPI_MakeWire mk(e1); mk.Add(e2);
        if (!mk.IsDone()) return TopoDS_Wire();
        return mk.Wire();
    } catch (...) { return TopoDS_Wire(); }
}

// The whole fallback: rebuild every section tip-split along the loft set's
// longest axis and loft again. Returns null on any failure.
TopoDS_Shape tipSplitLoft(const std::vector<TopoDS_Wire>& profiles, bool solid, bool ruled) {
    Bnd_Box bb;
    for (const auto& w : profiles) { try { BRepBndLib::Add(w, bb); } catch (...) {} }
    if (bb.IsVoid()) return TopoDS_Shape();
    double x0,y0,z0,x1,y1,z1; bb.Get(x0,y0,z0,x1,y1,z1);
    const double dx=x1-x0, dy=y1-y0, dz=z1-z0;
    const int axis = (dx >= dy && dx >= dz) ? 0 : (dy >= dz ? 1 : 2);
    try {
        BRepOffsetAPI_ThruSections t(solid ? Standard_True : Standard_False,
                                     ruled ? Standard_True : Standard_False);
        for (const auto& w : profiles) {
            const TopoDS_Wire sw = tipSplitWire(w, axis);
            if (sw.IsNull()) return TopoDS_Shape();
            t.AddWire(sw);
        }
        t.Build();
        if (!t.IsDone() || t.Shape().IsNull()) return TopoDS_Shape();
        if (!BRepCheck_Analyzer(t.Shape()).IsValid()) return TopoDS_Shape();
        return t.Shape();
    } catch (...) { return TopoDS_Shape(); }
}

void describeSections(const std::vector<TopoDS_Wire>& profiles) {
    gp_Vec prev(0, 0, 0);
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const TopoDS_Wire& w = profiles[i];
        if (w.IsNull()) { std::fprintf(stderr, "[Loft] section %zu: NULL\n", i); continue; }
        int edges = 0;
        for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) ++edges;
        double len = 0.0;
        try { BRepAdaptor_CompCurve cc(w); len = GCPnts_AbscissaPoint::Length(cc); } catch (...) {}
        Bnd_Box bb; try { BRepBndLib::Add(w, bb); } catch (...) {}
        double x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;
        if (!bb.IsVoid()) bb.Get(x0,y0,z0,x1,y1,z1);
        const gp_Vec n = wireWinding(w);
        const double dot = (i == 0) ? 1.0 : n.Dot(prev);
        std::fprintf(stderr,
            "[Loft] section %zu: %d edges, closed=%s, perimeter %.3f, "
            "winding (%.3f,%.3f,%.3f), dot-with-previous %+.3f%s\n",
            i, edges, BRep_Tool::IsClosed(w) ? "yes" : "NO", len,
            n.X(), n.Y(), n.Z(), dot,
            (i > 0 && dot < 0.0) ? "   <-- OPPOSITE WINDING, will twist" : "");
        std::fprintf(stderr,
            "[Loft]            bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f\n",
            x0, x1, y0, y1, z0, z1);
        prev = n;
    }
}

} // namespace

LoftOp::LoftOp() = default;

void LoftOp::addProfile(const TopoDS_Wire& wire) {
    m_profiles.push_back(wire);
    m_holeProfiles.emplace_back(); // no holes for this profile
}

void LoftOp::addProfile(const TopoDS_Wire& outer, const std::vector<TopoDS_Wire>& holes) {
    m_profiles.push_back(outer);
    m_holeProfiles.push_back(holes);
}

void LoftOp::clearProfiles() {
    m_profiles.clear();
    m_holeProfiles.clear();
}

void LoftOp::setSolid(bool solid) {
    m_solid = solid;
}

void LoftOp::setRuled(bool ruled) {
    m_ruled = ruled;
}

bool LoftOp::execute(Document& doc) {
    if (m_profiles.size() < 2) {
        return false;
    }

    try {
        // Degenerate section stacks (e.g. perpendicular "wall" profiles that
        // make the surface fold through itself) can drive ThruSections to a
        // kernel FAULT, not just a clean failure. OCC_CATCH_SIGNALS turns that
        // signal into a Standard_Failure the catch below absorbs — without it
        // the app dies (crash reproduced by repeated preview/cancel on a
        // weaving 3-section loft).
        OCC_CATCH_SIGNALS
        BRepOffsetAPI_ThruSections thruSections(m_solid ? Standard_True : Standard_False,
                                                 m_ruled ? Standard_True : Standard_False);

        describeSections(m_profiles);
        dumpSections(m_profiles);

        std::vector<TopoDS_Wire> profiles = m_profiles;

        for (const auto& wire : profiles) {
            thruSections.AddWire(wire);
        }

        thruSections.Build();
        if (!thruSections.IsDone()) {
            return false;
        }

        TopoDS_Shape loftedShape = thruSections.Shape();

        // A loft that folds through itself still reads as "valid" to BRepCheck
        // and has a plausible volume, so the fold must be looked for
        // explicitly. Only sections without holes take the fallback -- the
        // tip-split rebuild does not carry hole channels through.
        bool holesPresent = false;
        for (const auto& hp : m_holeProfiles) if (!hp.empty()) { holesPresent = true; break; }
        if (!holesPresent && !loftedShape.IsNull() && selfIntersects(loftedShape)) {
            std::fprintf(stderr, "[Loft] result self-intersects -- retrying with "
                                 "tip-split sections.\n");
            const TopoDS_Shape retry = tipSplitLoft(profiles, m_solid, m_ruled);
            if (!retry.IsNull() && !selfIntersects(retry)) {
                std::fprintf(stderr, "[Loft] tip-split loft is clean -- using it.\n");
                loftedShape = retry;
            } else {
                std::fprintf(stderr, "[Loft] tip-split retry did not help; keeping "
                                     "the original result.\n");
            }
        }

        // Tube support: if the profiles carry holes (e.g. concentric circles),
        // loft each hole-channel into its own inner solid and cut it from the
        // outer loft. Only meaningful for a solid loft. Hole k is matched by
        // index across the profiles, and we require every profile to expose the
        // same number of holes so the channels pair up unambiguously.
        if (m_solid && !m_holeProfiles.empty()) {
            size_t nHoles = m_holeProfiles[0].size();
            bool uniform = nHoles > 0;
            for (const auto& hp : m_holeProfiles) {
                if (hp.size() != nHoles) { uniform = false; break; }
            }
            for (size_t k = 0; uniform && k < nHoles; ++k) {
                BRepOffsetAPI_ThruSections inner(Standard_True, // solid
                                                 m_ruled ? Standard_True : Standard_False);
                for (const auto& hp : m_holeProfiles) {
                    inner.AddWire(hp[k]);
                }
                inner.Build();
                if (!inner.IsDone()) continue; // skip a hole that won't loft
                BRepAlgoAPI_Cut cut(loftedShape, inner.Shape());
                cut.Build();
                if (!cut.IsDone()) continue;
                // Adopt the cut only if it's still a usable solid — a bad hole
                // channel can yield a null/empty/invalid result that would
                // otherwise replace a perfectly good outer loft.
                TopoDS_Shape cutShape = cut.Shape();
                if (cutShape.IsNull()) continue;
                GProp_GProps cutProps;
                BRepGProp::VolumeProperties(cutShape, cutProps);
                if (cutProps.Mass() < 1e-6) continue;
                if (!BRepCheck_Analyzer(cutShape).IsValid()) continue;
                loftedShape = cutShape;
            }
        }

        // Validate-or-refuse (same gate as BooleanOp/FilletOp/ShellOp): a
        // degenerate section stack can pass IsDone() yet produce a null or
        // topologically invalid shape that later crashes tessellation/save.
        // The volume check only applies to solid lofts — a surface loft
        // legitimately encloses no volume.
        if (loftedShape.IsNull()) return false;
        if (m_solid) {
            GProp_GProps gp;
            BRepGProp::VolumeProperties(loftedShape, gp);
            if (gp.Mass() < 1e-6) return false;
        }
        if (!BRepCheck_Analyzer(loftedShape).IsValid()) return false;

        doc.addOrPutBody(m_createdBodyId, loftedShape, "Loft");

        return true;
    } catch (...) {
        return false;
    }
}

bool LoftOp::undo(Document& doc) {
    try {
        if (m_createdBodyId >= 0) {
            doc.removeBody(m_createdBodyId);
            // Keep m_createdBodyId — tombstone restore on next execute().
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string LoftOp::description() const {
    std::string desc = "Loft through " + std::to_string(m_profiles.size()) + " profiles";
    if (m_solid) {
        desc += " (Solid)";
    } else {
        desc += " (Shell)";
    }
    if (m_ruled) {
        desc += " Ruled";
    }
    return desc;
}

void LoftOp::renderProperties() {
    ImGui::Text("Loft");
    ImGui::Separator();

    ImGui::Text("Profiles: %d", static_cast<int>(m_profiles.size()));

    if (m_profiles.size() < 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "At least 2 profiles required");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "%d profiles ready", static_cast<int>(m_profiles.size()));
    }

    ImGui::Separator();
    ImGui::Checkbox("Solid", &m_solid);
    ImGui::Checkbox("Ruled Surface", &m_ruled);
}

OperationDiff LoftOp::captureDiff() const {
    OperationDiff d;
    if (m_createdBodyId >= 0) d.created.push_back(m_createdBodyId);
    return d;
}

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <sstream>

std::string LoftOp::serializeParams() const {
    // Profiles are raw wires (picked from sketch regions / body loops at
    // create time) — no persistent source ids exist, so they persist as an
    // ASCII BREP compound embedded in the params. PARAMS_LEN stores raw
    // bytes, so the multi-line BREP is safe; it goes LAST, length-prefixed.
    // Compound order: profile0, its holes..., profile1, its holes..., etc.
    std::string blob = "solid=" + std::to_string(m_solid ? 1 : 0) +
                       ";ruled=" + std::to_string(m_ruled ? 1 : 0) +
                       ";created=" + std::to_string(m_createdBodyId) +
                       ";np=" + std::to_string(m_profiles.size());
    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const size_t nh = i < m_holeProfiles.size() ? m_holeProfiles[i].size() : 0;
        blob += ";h" + std::to_string(i) + "=" + std::to_string(nh);
        bb.Add(comp, m_profiles[i]);
        for (size_t j = 0; j < nh; ++j) bb.Add(comp, m_holeProfiles[i][j]);
    }
    std::ostringstream os;
    BRepTools::Write(comp, os);
    const std::string brep = os.str();
    blob += ";brep=" + std::to_string(brep.size()) + ":" + brep;
    return blob;
}

bool LoftOp::deserializeParams(const std::string& blob) {
    m_profiles.clear();
    m_holeProfiles.clear();
    std::vector<int> holeCounts;
    int np = 0;
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = blob.substr(pos, eq - pos);
        if (key == "brep") {
            // <len>:<raw ascii brep>, runs to end.
            size_t colon = blob.find(':', eq);
            if (colon == std::string::npos) break;
            size_t n = static_cast<size_t>(
                std::atoll(blob.substr(eq + 1, colon - eq - 1).c_str()));
            if (colon + 1 + n > blob.size()) break;
            std::istringstream is(blob.substr(colon + 1, n));
            TopoDS_Shape comp;
            BRep_Builder bb;
            try { BRepTools::Read(comp, is, bb); } catch (...) { return false; }
            // Unpack: per profile i, one wire + holeCounts[i] hole wires.
            TopoDS_Iterator it(comp);
            for (int i = 0; i < np && it.More(); ++i) {
                if (it.Value().ShapeType() != TopAbs_WIRE) return false;
                m_profiles.push_back(TopoDS::Wire(it.Value()));
                it.Next();
                std::vector<TopoDS_Wire> holes;
                int nh = i < static_cast<int>(holeCounts.size()) ? holeCounts[i] : 0;
                for (int j = 0; j < nh && it.More(); ++j) {
                    holes.push_back(TopoDS::Wire(it.Value()));
                    it.Next();
                }
                m_holeProfiles.push_back(std::move(holes));
            }
            any = true;
            break;
        }
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "solid")   { m_solid = val == "1"; any = true; }
        else if (key == "ruled")   { m_ruled = val == "1"; any = true; }
        else if (key == "created") { m_createdBodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "np")      { np = std::atoi(val.c_str()); any = true; }
        else if (!key.empty() && key[0] == 'h') {
            int idx = std::atoi(key.c_str() + 1);
            if (idx >= static_cast<int>(holeCounts.size()))
                holeCounts.resize(idx + 1, 0);
            holeCounts[idx] = std::atoi(val.c_str());
        }
        pos = end + 1;
    }
    return any && static_cast<int>(m_profiles.size()) == np && np >= 2;
}

bool LoftOp::rehydrateFromReload(const ReloadState& state, Document&) {
    if (m_profiles.size() < 2) return false;
    if (m_createdBodyId < 0 && !state.created.empty())
        m_createdBodyId = state.created.front();
    return true;   // profiles are self-contained; execute() re-lofts them
}
