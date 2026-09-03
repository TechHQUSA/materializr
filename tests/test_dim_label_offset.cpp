// Dimension label placement offsets.
//
// Reported symptom: "I can't move these stupid dimension tags to a better
// location, each click just opens the edit." Labels stored an offset only at
// creation and nothing ever rewrote it, so a press went straight to the value
// editor. Dragging now rewrites Constraint::labelOffX/Y through dimLabelOffset.
//
// The trap this pins down: (0,0) is ALSO the sentinel for "never placed", which
// the renderer reads as "use the automatic position". A tag dropped exactly on
// its anchor must not silently snap back to auto placement.

#include "modeling/SketchConstraints.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(DimLabelOffset, IsRelativeToTheAnchor) {
    double x = 0, y = 0;
    // Anchor at (10,5), dropped at (13,9) -> the tag sits +3,+4 from its
    // anchor, so it keeps that relationship when the solver later moves the
    // geometry underneath it.
    materializr::dimLabelOffset(13.0, 9.0, 10.0, 5.0, x, y);
    EXPECT_DOUBLE_EQ(3.0, x);
    EXPECT_DOUBLE_EQ(4.0, y);
}

TEST(DimLabelOffset, NegativeOffsetsSurvive) {
    double x = 0, y = 0;
    materializr::dimLabelOffset(2.0, 1.0, 10.0, 5.0, x, y);
    EXPECT_DOUBLE_EQ(-8.0, x);
    EXPECT_DOUBLE_EQ(-4.0, y);
}

// Dropping a label exactly on its anchor produces (0,0) — the "never placed"
// sentinel. Storing that verbatim would make the label jump to its automatic
// position, reading as "the drag was ignored".
TEST(DimLabelOffset, ExactAnchorDropDoesNotBecomeTheUnplacedSentinel) {
    double x = 0, y = 0;
    materializr::dimLabelOffset(7.5, -2.25, 7.5, -2.25, x, y);
    EXPECT_FALSE(x == 0.0 && y == 0.0)
        << "offset collapsed to the unplaced sentinel; the label would revert "
           "to automatic placement";
    // The nudge must be far below anything visible — a tenth of a micron.
    EXPECT_LT(std::abs(x), 1e-3);
    EXPECT_LT(std::abs(y), 1e-3);
}

// One axis landing on zero is ordinary and must be preserved exactly: a label
// dragged straight up from its anchor has no horizontal offset.
TEST(DimLabelOffset, SingleZeroAxisIsLeftAlone) {
    double x = 0, y = 0;
    materializr::dimLabelOffset(10.0, 12.0, 10.0, 5.0, x, y);
    EXPECT_DOUBLE_EQ(0.0, x);
    EXPECT_DOUBLE_EQ(7.0, y);
}
