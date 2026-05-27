namespace materializr { namespace force_link {

// Phase 1 plugins
void forceLink_CoreCommands();
void forceLink_StepIO();
void forceLink_IgesIO();
void forceLink_StlExport();
void forceLink_GltfExport();
void forceLink_ImageExport();

// Phase 2 plugins
void forceLink_Boolean();
void forceLink_Delete();
void forceLink_Transform();
void forceLink_Mirror();
void forceLink_Copy();
void forceLink_Pattern();
void forceLink_Shell();
void forceLink_OffsetFace();
void forceLink_SplitBody();
void forceLink_ConstructionPlane();

// Phase 3 plugins
void forceLink_Fillet();
void forceLink_Chamfer();
void forceLink_Extrude();
void forceLink_PushPull();

// Phase 4 plugins
void forceLink_Sketch();
void forceLink_GizmoDrag();

void linkAll() {
    // Phase 1
    forceLink_CoreCommands();
    forceLink_StepIO();
    forceLink_IgesIO();
    forceLink_StlExport();
    forceLink_GltfExport();
    forceLink_ImageExport();
    // Phase 2
    forceLink_Boolean();
    forceLink_Delete();
    forceLink_Transform();
    forceLink_Mirror();
    forceLink_Copy();
    forceLink_Pattern();
    forceLink_Shell();
    forceLink_OffsetFace();
    forceLink_SplitBody();
    forceLink_ConstructionPlane();
    // Phase 3
    forceLink_Fillet();
    forceLink_Chamfer();
    forceLink_Extrude();
    forceLink_PushPull();
    // Phase 4
    forceLink_Sketch();
    forceLink_GizmoDrag();
}

}} // namespace materializr::force_link
