#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>

namespace materializr { class Sketch; class EventBus; }

struct BodyEntry {
    int id;
    std::string name;
    TopoDS_Shape shape;
    bool visible = true;
    glm::vec3 color = glm::vec3(0.80f, 0.80f, 0.82f); // default: light grey
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
    // Add a body with an explicit id, or update the body that already has that
    // id. Keeps ids stable across save/load and history replay; bumps the id
    // counter so later auto-assigned ids don't collide.
    void putBody(int id, const TopoDS_Shape& shape, const std::string& name = "");
    const TopoDS_Shape& getBody(int id) const;
    std::string getBodyName(int id) const;
    void setBodyName(int id, const std::string& name);
    void setBodyVisible(int id, bool visible);
    bool isBodyVisible(int id) const;
    glm::vec3 getBodyColor(int id) const;
    void setBodyColor(int id, const glm::vec3& color);
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
