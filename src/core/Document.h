#pragma once
#include <string>
#include <vector>
#include <memory>
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

namespace materializr { class Sketch; class EventBus; }

struct BodyEntry {
    int id;
    std::string name;
    TopoDS_Shape shape;
    bool visible = true;
};

struct PlaneEntry {
    int id;
    std::string name;
    gp_Pln plane;
};

struct SketchEntry {
    int id;
    std::string name;
    std::shared_ptr<materializr::Sketch> sketch;
    bool visible = true;
};

class Document {
public:
    Document();
    ~Document();

    void setEventBus(materializr::EventBus* bus) { m_eventBus = bus; }

    // Body management
    int addBody(const TopoDS_Shape& shape, const std::string& name = "");
    void removeBody(int id);
    void updateBody(int id, const TopoDS_Shape& shape);
    const TopoDS_Shape& getBody(int id) const;
    std::string getBodyName(int id) const;
    void setBodyName(int id, const std::string& name);
    void setBodyVisible(int id, bool visible);
    bool isBodyVisible(int id) const;
    std::vector<int> getAllBodyIds() const;

    // Sketch management
    int addSketch(std::shared_ptr<materializr::Sketch> sketch, const std::string& name = "");
    void removeSketch(int id);
    std::shared_ptr<materializr::Sketch> getSketch(int id) const;
    std::string getSketchName(int id) const;
    void setSketchName(int id, const std::string& name);
    void setSketchVisible(int id, bool visible);
    bool isSketchVisible(int id) const;
    std::vector<int> getAllSketchIds() const;
    int sketchCount() const;

    // Construction planes
    int addPlane(const gp_Pln& plane, const std::string& name = "");

    // Clear everything
    void clear();

    // Body count
    int bodyCount() const;

private:
    int findBodyIndex(int id) const;
    int findSketchIndex(int id) const;

    std::vector<BodyEntry> m_bodies;
    std::vector<PlaneEntry> m_planes;
    std::vector<SketchEntry> m_sketches;
    int m_nextBodyId = 1;
    int m_nextPlaneId = 1;
    int m_nextSketchId = 1;
    materializr::EventBus* m_eventBus = nullptr;
};
