// Display units: the one place a length changes unit.
//
// This test deliberately includes NO ImGui header. Units.h must compile in a TU
// that has never seen imgui.h — any ImGui call creeping into it (the Round-2
// review caught a ClearActiveID() that would have crashed before the context
// existed) fails right here at compile time.

#include "core/Units.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using materializr::LengthUnit;

namespace {

// Every test that changes the global unit restores it on exit, including on an
// early ASSERT failure — otherwise one failing case would poison the rest.
struct ScopedUnit {
    LengthUnit saved;
    explicit ScopedUnit(LengthUnit u) : saved(materializr::currentUnit()) { materializr::setCurrentUnit(u); }
    ~ScopedUnit() { materializr::setCurrentUnit(saved); }
};

const LengthUnit kAll[] = { LengthUnit::Mm, LengthUnit::Cm, LengthUnit::M,
                            LengthUnit::In, LengthUnit::Ft };

} // namespace

TEST(Units, DefaultIsMillimetres) {
    EXPECT_EQ(LengthUnit::Mm, materializr::currentUnit());
    EXPECT_STREQ("mm", materializr::unitSuffix());
}

// 1. mm -> display -> mm is the identity, to well below anything printed.
TEST(Units, RoundTripExact) {
    for (LengthUnit u : kAll) {
        ScopedUnit s(u);
        for (double x : { 0.0, 0.001, 1.0, 25.4, 304.8, 1234.5678, 1e6 })
            EXPECT_NEAR(x, materializr::toMm(materializr::toDisplay(x)), 1e-9) << "unit " << int(u);
    }
}

// 2. The same through a FLOAT shadow — many Op members are float. The error
// must stay below what the unit's own precision can show, or an untouched
// value would visibly drift after one edit.
TEST(Units, FloatMemberRoundTrip) {
    for (LengthUnit u : kAll) {
        ScopedUnit s(u);
        const double resolutionMm = std::pow(10.0, -materializr::unitInfo(u).decimals)
                                  * materializr::unitInfo(u).toMm;
        for (float x : { 0.5f, 25.4f, 100.0f, 1234.5f }) {
            const float disp = static_cast<float>(materializr::toDisplay(x));
            const float back = static_cast<float>(materializr::toMm(disp));
            EXPECT_LT(std::fabs(back - x), resolutionMm) << "unit " << int(u) << " x=" << x;
        }
    }
}

// 3. Exact strings, all five units, length / area / volume.
TEST(Units, FormatEachUnit) {
    const double mm = 25.4, mm2 = 645.16, mm3 = 16387.064;   // 1 in, 1 in², 1 in³
    struct Row { LengthUnit u; const char* len; const char* area; const char* vol; };
    const Row rows[] = {
        { LengthUnit::Mm, "25.40 mm",  "645.16 mm\xC2\xB2",   "16387.06 mm\xC2\xB3" },
        { LengthUnit::Cm, "2.540 cm",  "6.452 cm\xC2\xB2",    "16.387 cm\xC2\xB3"   },
        { LengthUnit::M,  "0.0254 m",  "0.0006 m\xC2\xB2",    "0.0000 m\xC2\xB3"    },
        { LengthUnit::In, "1.000 in",  "1.000 in\xC2\xB2",    "1.000 in\xC2\xB3"    },
        { LengthUnit::Ft, "0.0833 ft", "0.0069 ft\xC2\xB2",   "0.0006 ft\xC2\xB3"   },
    };
    for (const Row& r : rows) {
        ScopedUnit s(r.u);
        EXPECT_EQ(std::string(r.len),  materializr::fmtLength(mm))  << "unit " << int(r.u);
        EXPECT_EQ(std::string(r.area), materializr::fmtArea(mm2))   << "unit " << int(r.u);
        EXPECT_EQ(std::string(r.vol),  materializr::fmtVolume(mm3)) << "unit " << int(r.u);
    }
}

// 4. A trailing unit token overrides the current unit. Longest match first:
// "5m" is metres, not mm.
TEST(Units, ParseSuffixes) {
    ScopedUnit s(LengthUnit::Mm);
    double mm = -1;
    EXPECT_TRUE(materializr::parseLength("25.4mm", mm)); EXPECT_DOUBLE_EQ(25.4,  mm);
    EXPECT_TRUE(materializr::parseLength("1in",    mm)); EXPECT_DOUBLE_EQ(25.4,  mm);
    EXPECT_TRUE(materializr::parseLength("2\"",    mm)); EXPECT_DOUBLE_EQ(50.8,  mm);
    EXPECT_TRUE(materializr::parseLength("3ft",    mm)); EXPECT_DOUBLE_EQ(914.4, mm);
    EXPECT_TRUE(materializr::parseLength("3'",     mm)); EXPECT_DOUBLE_EQ(914.4, mm);
    EXPECT_TRUE(materializr::parseLength("2cm",    mm)); EXPECT_DOUBLE_EQ(20.0,  mm);
    EXPECT_TRUE(materializr::parseLength("5m",     mm)); EXPECT_DOUBLE_EQ(5000.0, mm) << "'5m' must be metres";
    EXPECT_TRUE(materializr::parseLength("1IN",    mm)); EXPECT_DOUBLE_EQ(25.4,  mm);
    EXPECT_TRUE(materializr::parseLength(" 1 in ", mm)); EXPECT_DOUBLE_EQ(25.4,  mm);
}

// 5. No suffix means "whatever the user is looking at".
TEST(Units, ParseNoSuffixUsesCurrentUnit) {
    double mm = -1;
    { ScopedUnit s(LengthUnit::In); EXPECT_TRUE(materializr::parseLength("1",   mm)); EXPECT_DOUBLE_EQ(25.4,  mm); }
    { ScopedUnit s(LengthUnit::Ft); EXPECT_TRUE(materializr::parseLength("0.5", mm)); EXPECT_DOUBLE_EQ(152.4, mm); }
    { ScopedUnit s(LengthUnit::Mm); EXPECT_TRUE(materializr::parseLength("7",   mm)); EXPECT_DOUBLE_EQ(7.0,   mm); }
}

// 6. Garbage is refused and the output is left alone — parseFinite's contract.
TEST(Units, ParseRejectsGarbage) {
    ScopedUnit s(LengthUnit::Mm);
    double mm = 42.0;
    EXPECT_FALSE(materializr::parseLength("abc",     mm));
    EXPECT_FALSE(materializr::parseLength("1e999in", mm));   // inf
    EXPECT_FALSE(materializr::parseLength("",        mm));
    EXPECT_FALSE(materializr::parseLength("   ",     mm));
    EXPECT_FALSE(materializr::parseLength("in",      mm));   // suffix, no number
    EXPECT_FALSE(materializr::parseLength(nullptr,   mm));
    EXPECT_DOUBLE_EQ(42.0, mm) << "a refusal must not touch the output";
}

// 7. Expressions are refused. Formulas are mm and are out of this parser's
// scope — accepting "10+5" here (strtod would read the 10) is exactly how a
// variable-bearing formula would get scaled by the display unit.
TEST(Units, ParseRejectsExpressions) {
    ScopedUnit s(LengthUnit::In);
    double mm = 42.0;
    EXPECT_FALSE(materializr::parseLength("width/2", mm));
    EXPECT_FALSE(materializr::parseLength("10+5",    mm));
    EXPECT_FALSE(materializr::parseLength("2*in",    mm));
    EXPECT_FALSE(materializr::parseLength("1 2",     mm));
    EXPECT_FALSE(materializr::parseLength("1in2",    mm));
    EXPECT_DOUBLE_EQ(42.0, mm);
}

// Area and volume scale by the factor squared and cubed, not the factor.
TEST(Units, AreaVolumeFactors) {
    ScopedUnit s(LengthUnit::In);
    EXPECT_NEAR(1.0, materializr::areaToDisplay(645.16),   1e-9);
    EXPECT_NEAR(1.0, materializr::volToDisplay(16387.064), 1e-9);
}

// Out-of-range enum values fall back to mm instead of indexing past the table
// (a hand-edited settings file is the realistic source).
TEST(Units, OutOfRangeUnitFallsBackToMm) {
    EXPECT_STREQ("mm", materializr::unitInfo(static_cast<LengthUnit>(99)).suffix);
    EXPECT_STREQ("mm", materializr::unitInfo(static_cast<LengthUnit>(-1)).suffix);
}

// 16. The RAII restorer every test relies on actually restores — including when
// the scope is left early.
TEST(Units, ScopedUnitRestores) {
    ASSERT_EQ(LengthUnit::Mm, materializr::currentUnit());
    {
        ScopedUnit s(LengthUnit::Ft);
        EXPECT_EQ(LengthUnit::Ft, materializr::currentUnit());
    }
    EXPECT_EQ(LengthUnit::Mm, materializr::currentUnit());
    auto earlyReturn = [] { ScopedUnit s(LengthUnit::Cm); return; };
    earlyReturn();
    EXPECT_EQ(LengthUnit::Mm, materializr::currentUnit());
}
