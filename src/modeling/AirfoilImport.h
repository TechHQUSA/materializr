#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace materializr {

class Sketch;

// Airfoil coordinate file -> sketch geometry.
//
// The de-facto interchange format for aerofoil sections (airfoiltools.com, UIUC
// and every wing-design tool) is a plain list of chord-normalised x/y pairs.
// Two dialects are in circulation and users do not reliably know which they
// have, so both are accepted:
//
//   SELIG    one continuous loop: trailing edge -> over the top -> leading
//            edge -> back under -> trailing edge.
//   LEDNICER a count line ("61. 61."), then the upper surface leading edge ->
//            trailing edge, then the lower surface the same way.
//
// Coordinates are ON-CURVE SAMPLES, not control points, which is why the
// surfaces become interpolating splines (Sketch's splines pass through every
// control point) rather than approximations.
struct AirfoilProfile {
    std::string name;                 // header line, or the filename stem
    std::vector<glm::vec2> upper;     // leading edge -> trailing edge, chord 0..1
    std::vector<glm::vec2> lower;     // leading edge -> trailing edge
    // A blunt (open) trailing edge is normal on a printable or machined
    // section -- a knife edge cannot be manufactured -- so the gap is measured
    // rather than quietly closed, and place() bridges it with a straight edge.
    bool  bluntTrailingEdge = false;
    float trailingGap = 0.0f;         // normalised chord units, 0 = sharp
    bool empty() const { return upper.size() < 2 || lower.size() < 2; }
    // Max thickness and camber as fractions of chord — what the section is
    // usually identified by, and a cheap sanity check on a parse.
    float thickness() const;
    float camber() const;
};

class AirfoilImport {
public:
    // Parse file contents. `err` (optional) gets a one-line human reason on
    // failure. Both dialects are auto-detected.
    static bool parse(const std::string& text, AirfoilProfile& out,
                      std::string* err = nullptr);
    static bool load(const std::string& path, AirfoilProfile& out,
                     std::string* err = nullptr);

    // Reduce each surface to at most `maxPerSurface` points, keeping the shape
    // within `tolerance` (normalised chord) by Douglas-Peucker. A 160-point
    // file makes a spline the solver and the region walker both labour over;
    // the leading edge keeps its density because that is where curvature is.
    static void simplify(AirfoilProfile& prof, int maxPerSurface,
                         float tolerance = 0.0005f);

    // Insert into the sketch: chord along +X from `pos`, scaled so the chord
    // is `chordMm`, rotated `angleDeg` CCW about the leading edge (angle of
    // incidence / washout). Emits two splines plus a trailing-edge line when
    // the section is blunt. Returns the number of elements added, 0 on failure.
    static int place(Sketch* sketch, const AirfoilProfile& prof, glm::vec2 pos,
                     float chordMm, float angleDeg);
};

} // namespace materializr
