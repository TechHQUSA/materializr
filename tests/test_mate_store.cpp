// Mates live on the Document the way constraints live on a Sketch: stored
// with the model, not replayed as history steps.

#include "core/Document.h"
#include "modeling/Mate.h"
#include "io/ProjectIO.h"
#include "test_tmp_path.h"

#include <gtest/gtest.h>
#include <BRepPrimAPI_MakeBox.hxx>

using materializr::Mate;
using materializr::MateType;

namespace {
Mate makeFasten(int a, int b, double offset) {
    Mate m{};
    m.type = MateType::Fasten;
    m.bodyA = a;
    m.bodyB = b;
    m.offset = offset;
    return m;
}
} // namespace

TEST(MateStore, AddReturnsUniqueIdsAndStores) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");

    int m1 = doc.addMate(makeFasten(a, b, 2.0));
    int m2 = doc.addMate(makeFasten(b, a, 3.0));

    EXPECT_NE(m1, m2);
    ASSERT_EQ(doc.getMates().size(), 2u);
    EXPECT_EQ(doc.getMates()[0].id, m1);
    EXPECT_DOUBLE_EQ(doc.getMates()[0].offset, 2.0);
}

TEST(MateStore, RemoveDropsOnlyTheNamedMate) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");

    int m1 = doc.addMate(makeFasten(a, b, 2.0));
    int m2 = doc.addMate(makeFasten(b, a, 3.0));
    doc.removeMate(m1);

    ASSERT_EQ(doc.getMates().size(), 1u);
    EXPECT_EQ(doc.getMates()[0].id, m2);
}

TEST(MateStore, GroundedBodyDefaultsToNoneAndIsSettable) {
    Document doc;
    EXPECT_EQ(doc.getGroundedBody(), -1);
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    doc.setGroundedBody(a);
    EXPECT_EQ(doc.getGroundedBody(), a);
}


TEST(MatePersistence, RoundTripsThroughProjectFile) {
    Document doc;
    int a = doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    int b = doc.addBody(BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape(), "B");
    doc.setGroundedBody(a);

    Mate m = makeFasten(a, b, 7.5);
    m.angle = 0.25;
    m.flipped = true;
    int mid = doc.addMate(m);

    std::string path = mzrtest::tmpPath("mate_roundtrip.mzr");
    ASSERT_TRUE(materializr::ProjectIO::save(path, doc).success);

    Document loaded;
    ASSERT_TRUE(materializr::ProjectIO::load(path, loaded).success);

    ASSERT_EQ(loaded.getMates().size(), 1u);
    const Mate& r = loaded.getMates()[0];
    EXPECT_EQ(r.id, mid);
    EXPECT_EQ(r.type, MateType::Fasten);
    EXPECT_EQ(r.bodyA, a);
    EXPECT_EQ(r.bodyB, b);
    EXPECT_DOUBLE_EQ(r.offset, 7.5);
    EXPECT_DOUBLE_EQ(r.angle, 0.25);
    EXPECT_TRUE(r.flipped);
    EXPECT_EQ(loaded.getGroundedBody(), a);
}

TEST(MatePersistence, FileWithNoMateBlockLoadsClean) {
    // Every project written before mates existed has no MATE_COUNT block. It
    // must load with zero mates and no grounded body rather than failing.
    Document doc;
    doc.addBody(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), "A");
    std::string path = mzrtest::tmpPath("mate_legacy.mzr");
    ASSERT_TRUE(materializr::ProjectIO::save(path, doc).success);

    Document loaded;
    ASSERT_TRUE(materializr::ProjectIO::load(path, loaded).success);
    EXPECT_TRUE(loaded.getMates().empty());
    EXPECT_EQ(loaded.getGroundedBody(), -1);
}
