#pragma once

#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Core/Pass/ComputePass.h"
#include "Core/State/GraphicsState.h"
#include "Core/Program/Program.h"
#include "Core/Program/ProgramVars.h"
#include "Scene/TriangleMesh.h"

using namespace Falcor;

enum class DDGIDirtyFlags : uint8_t
{
    None = 0,
    Probes = 1u << 0,       // origin/spacing/counts changed
    Atlases = 1u << 1,      // tile res / counts changed
    RtPrograms = 1u << 2,   // scene or type conformance changed
    VizResources = 1u << 3, // viz sphere mesh/state changed
    BlendProgram = 1u << 4, // blend program (rare)
};

FALCOR_ENUM_CLASS_OPERATORS(DDGIDirtyFlags);

class DDGIPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(DDGIPass, "DDGIPass", "Standalone pass for specular lighting using DDGI.");

    static ref<DDGIPass> create(const ref<Device>& pDevice, const Properties& props) { return make_ref<DDGIPass>(pDevice, props); }

    DDGIPass(const ref<Device>& pDevice, const Properties& props);

    Properties getProperties() const override;
    RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    void renderUI(Gui::Widgets& widget) override;
    void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

private:
    struct Options
    {
        // Probe volume
        float3 origin = float3(0.f);
        float3 spacing = float3(1.f);
        uint3 probeCounts = uint3(8, 8, 8);

        // Trace/Radiance/Irradiance
        uint32_t tileResTrace = 16;     // per-probe tile resolution for trace outputs
        uint32_t tileResRadiance = 16;  // radiance atlas tile resolution
        uint32_t tileResIrradiance = 8; // irradiance atlas tile resolution
        uint32_t raysPerProbe = 288;
        float maxRayDistance = 100000.f;

        // Blend
        float giIntensity = 1.0f;

        // Visualization
        bool visualizeProbes = true;
        float probeVizRadius = 0.25f;
        float3 probeVizColor = float3(1.f);

        // Debug toggles
        bool enableTrace = true;
        bool enableRadiance = true;
        bool enableIrradiance = true;
        bool enableBlend = true;
    };

    struct Resources
    {
        ref<Buffer> probePositions;

        ref<Texture> hitPositionAtlas;
        ref<Texture> hitNormalAtlas;
        ref<Texture> hitAlbedoAtlas;

        ref<Texture> radianceAtlas;
        ref<Texture> irradianceAtlas;
        ref<Texture> depthMomentsAtlas;
    };

    void parseProperties(const Properties& props);

    // Pipeline Stages
    void stageGenerateProbes(RenderContext* ctx);
    void stageTraceProbeGBuffer(RenderContext* ctx) const;
    void stageComputeRadiance(RenderContext* ctx) const;
    void stageComputeIrradiance(RenderContext* ctx) const;
    void stageBlend(RenderContext* ctx, const RenderData& rd);
    void stageVisualize(RenderContext* ctx, const RenderData& rd);

    // Resource preparation
    void rebuildIfNeeded(RenderContext* ctx);
    void prepareProbePositionsBuffer();
    void prepareAtlases();
    void prepareTraceProgram();
    void prepareVizResources();
    void prepareBlendResources(const RenderData& rd);

    // Helpers
    uint32_t getProbeCount() const { return mOpt.probeCounts.x * mOpt.probeCounts.y * mOpt.probeCounts.z; }

    uint2 getAtlasDims(const uint32_t tileRes) const
    {
        const uint32_t w = mOpt.probeCounts.x * tileRes;
        const uint32_t h = mOpt.probeCounts.y * mOpt.probeCounts.z * tileRes;
        return {w, h};
    }

    ref<Scene> mpScene;

    Options mOpt;
    DDGIDirtyFlags mDirty = DDGIDirtyFlags::Probes | DDGIDirtyFlags::Atlases | DDGIDirtyFlags::RtPrograms | DDGIDirtyFlags::VizResources |
                            DDGIDirtyFlags::BlendProgram;

    bool mOptionsChanged = false;

    ref<ComputePass> mpGenerateProbesPass;
    ref<ComputePass> mpRadiancePass;
    ref<ComputePass> mpIrradiancePass;

    ref<Program> mpTraceProgram;
    ref<RtBindingTable> mpTraceSBT;
    ref<RtProgramVars> mpTraceVars;

    ref<Program> mpBlendProgram;
    ref<GraphicsState> mpBlendState;
    ref<ProgramVars> mpBlendVars;

    ref<TriangleMesh> mpProbeSphere;
    ref<Vao> mpProbeSphereVao;
    ref<Program> mpVizProgram;
    ref<GraphicsState> mpVizState;
    ref<ProgramVars> mpVizVars;
    ref<Fbo> mpVizFbo;
    ref<Texture> mpVizFboColor;
    ref<Texture> mpVizFboDepth;

    // GPU Resources
    ref<Buffer> mpProbePositions;

    // Trace outputs (probe-space "GBuffer" atlases)
    ref<Texture> mpHitPosAtlas;    // RGBA32Float: xyz=wsPos
    ref<Texture> mpHitNormalAtlas; // RGBA16Float: xyz=wsNormal
    ref<Texture> mpHitAlbedoAtlas; // RGBA16Float: rgb=albedo

    // Radiance/Irradiance
    ref<Texture> mpRadianceAtlas;   // RGBA16Float
    ref<Texture> mpIrradianceAtlas; // RGBA16Float

    // Reusable FBOs
    ref<Fbo> mpBlendFbo;
    ref<Texture> mpVizDepth; // cached depth for viz, if no depthIn wired

    // Sample generator
    ref<SampleGenerator> mpSampleGenerator;
    ref<Sampler> mpLinearSampler;

    uint32_t mFrameCount = 0;

    static constexpr auto kDepthIn = "depthIn";
    static constexpr auto kNormalIn = "normalIn";
    static constexpr auto kAlbedoIn = "albedoIn";
    static constexpr auto kEmissiveIn = "emissiveIn";
    static constexpr auto kColorOut = "color";
};
