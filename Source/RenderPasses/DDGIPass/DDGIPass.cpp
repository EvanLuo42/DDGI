#include "DDGIPass.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include <vector>

namespace
{
const std::string kGenerateProbesShader = "RenderPasses/DDGIPass/GenerateProbes.cs.slang";
const std::string kTraceGBufferShader = "RenderPasses/DDGIPass/TraceProbeGBuffer.rt.slang";
const std::string kComputeRadianceShader = "RenderPasses/DDGIPass/ComputeRadiance.cs.slang";
const std::string kComputeIrradianceShader = "RenderPasses/DDGIPass/ComputeIrradiance.cs.slang";
const std::string kBlendShader = "RenderPasses/DDGIPass/Blend.ps.slang";
const std::string kVisualizeShader = "RenderPasses/DDGIPass/VisualizeProbe.ps.slang";

constexpr char kOrigin[] = "origin";
constexpr char kSpacing[] = "spacing";
constexpr char kProbeCounts[] = "probeCounts";
constexpr char kTileResTrace[] = "tileResTrace";
constexpr char kTileResRadiance[] = "tileResRadiance";
constexpr char kTileResIrradiance[] = "tileResIrradiance";
constexpr char kRaysPerProbe[] = "raysPerProbe";
constexpr char kMaxRayDistance[] = "probeMaxRayDistance";
constexpr char kGIIntensity[] = "giIntensity";
constexpr char kVisualize[] = "visualizeProbes";
constexpr char kProbeVizRadius[] = "probeVizRadius";
constexpr char kProbeVizColor[] = "probeVizColor";
} // namespace

extern "C" FALCOR_API_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DDGIPass>();
}

DDGIPass::DDGIPass(const ref<Device>& pDevice, const Properties& props) : RenderPass(pDevice)
{
    parseProperties(props);

    mpGenerateProbesPass = ComputePass::create(mpDevice, kGenerateProbesShader, "main", DefineList(), true);

    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);
    FALCOR_ASSERT(mpSampleGenerator)

    mDirty |= DDGIDirtyFlags::BlendProgram;
}

void DDGIPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kOrigin)
            mOpt.origin = value, mDirty |= DDGIDirtyFlags::Probes;
        else if (key == kSpacing)
            mOpt.spacing = value, mDirty |= DDGIDirtyFlags::Probes;
        else if (key == kProbeCounts)
            mOpt.probeCounts = value, mDirty |= (DDGIDirtyFlags::Probes | DDGIDirtyFlags::Atlases);
        else if (key == kTileResTrace)
            mOpt.tileResTrace = value, mDirty |= DDGIDirtyFlags::Atlases;
        else if (key == kTileResRadiance)
            mOpt.tileResRadiance = value, mDirty |= DDGIDirtyFlags::Atlases;
        else if (key == kTileResIrradiance)
            mOpt.tileResIrradiance = value, mDirty |= DDGIDirtyFlags::Atlases;
        else if (key == kRaysPerProbe)
            mOpt.raysPerProbe = value;
        else if (key == kMaxRayDistance)
            mOpt.maxRayDistance = value;
        else if (key == kGIIntensity)
            mOpt.giIntensity = value;
        else if (key == kVisualize)
            mOpt.visualizeProbes = value;
        else if (key == kProbeVizRadius)
            mOpt.probeVizRadius = value, mDirty |= DDGIDirtyFlags::VizResources;
        else if (key == kProbeVizColor)
            mOpt.probeVizColor = value;
        else
            logWarning("Unknown property '{}' in DDGIPass properties.", key);
    }
}

Properties DDGIPass::getProperties() const
{
    Properties props;
    props[kOrigin] = mOpt.origin;
    props[kSpacing] = mOpt.spacing;
    props[kProbeCounts] = mOpt.probeCounts;
    props[kTileResTrace] = mOpt.tileResTrace;
    props[kTileResRadiance] = mOpt.tileResRadiance;
    props[kTileResIrradiance] = mOpt.tileResIrradiance;
    props[kRaysPerProbe] = mOpt.raysPerProbe;
    props[kMaxRayDistance] = mOpt.maxRayDistance;
    props[kGIIntensity] = mOpt.giIntensity;
    props[kVisualize] = mOpt.visualizeProbes;
    props[kProbeVizRadius] = mOpt.probeVizRadius;
    props[kProbeVizColor] = mOpt.probeVizColor;
    return props;
}

RenderPassReflection DDGIPass::reflect(const CompileData& compileData)
{
    RenderPassReflection r;

    r.addInput(kDepthIn, "Depth buffer").bindFlags(ResourceBindFlags::ShaderResource);
    r.addInput(kNormalIn, "World normal").bindFlags(ResourceBindFlags::ShaderResource).flags(RenderPassReflection::Field::Flags::Optional);
    r.addInput(kAlbedoIn, "Albedo buffer").bindFlags(ResourceBindFlags::ShaderResource).flags(RenderPassReflection::Field::Flags::Optional);
    r.addInput(kEmissiveIn, "Emissive buffer")
        .bindFlags(ResourceBindFlags::ShaderResource)
        .flags(RenderPassReflection::Field::Flags::Optional);

    r.addOutput(kColorOut, "Output color").bindFlags(ResourceBindFlags::RenderTarget);

    return r;
}

void DDGIPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;

    mFrameCount = 0;

    mpTraceProgram = nullptr;
    mpTraceSBT = nullptr;
    mpTraceVars = nullptr;

    mpBlendProgram = nullptr;
    mpBlendVars = nullptr;
    mDirty |= DDGIDirtyFlags::BlendProgram;

    if (!mpScene)
        return;

    auto defines = mpScene->getSceneDefines();
    defines.add(mpSampleGenerator->getDefines());

    mpRadiancePass = ComputePass::create(mpDevice, kComputeRadianceShader, "main", defines, true);
    mpIrradiancePass = ComputePass::create(mpDevice, kComputeIrradianceShader, "main", defines, true);

    const AABB b = mpScene->getSceneBounds();
    mOpt.origin = b.minPoint;
    const float3 extent = b.extent();
    mOpt.spacing = extent / float3(max(uint3(1), mOpt.probeCounts));

    mDirty |= DDGIDirtyFlags::Probes | DDGIDirtyFlags::Atlases | DDGIDirtyFlags::RtPrograms;
}

void DDGIPass::rebuildIfNeeded(RenderContext* ctx)
{
    if (is_set(mDirty, DDGIDirtyFlags::Probes))
    {
        prepareProbePositionsBuffer();
    }

    if (is_set(mDirty, DDGIDirtyFlags::Atlases))
    {
        prepareAtlases();
    }

    if (is_set(mDirty, DDGIDirtyFlags::RtPrograms))
    {
        prepareTraceProgram();
    }

    if (is_set(mDirty, DDGIDirtyFlags::VizResources))
    {
        prepareVizResources();
    }
}

void DDGIPass::prepareProbePositionsBuffer()
{
    if (const uint32_t probeCount = getProbeCount(); !mpProbePositions || mpProbePositions->getElementCount() < probeCount)
    {
        mpProbePositions = mpDevice->createStructuredBuffer(
            sizeof(float3),
            probeCount,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            MemoryType::DeviceLocal,
            nullptr,
            false
        );
        mpProbePositions->setName("DDGI::ProbePositions");
    }
}

void DDGIPass::prepareAtlases()
{
    const uint2 traceDim = getAtlasDims(mOpt.tileResTrace);
    const uint2 radDim = getAtlasDims(mOpt.tileResRadiance);
    const uint2 irrDim = getAtlasDims(mOpt.tileResIrradiance);

    auto ensureTex2D = [&](ref<Texture>& tex, const uint2 dim, const ResourceFormat fmt, const char* name, const ResourceBindFlags flags)
    {
        if (!tex || tex->getWidth() != dim.x || tex->getHeight() != dim.y || tex->getFormat() != fmt)
        {
            tex = mpDevice->createTexture2D(dim.x, dim.y, fmt, 1, 1, nullptr, flags);
            tex->setName(name);
        }
    };

    ensureTex2D(
        mpHitPosAtlas,
        traceDim,
        ResourceFormat::RGBA32Float,
        "DDGI::HitPosAtlas",
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    ensureTex2D(
        mpHitNormalAtlas,
        traceDim,
        ResourceFormat::RGBA16Float,
        "DDGI::HitNormalAtlas",
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    ensureTex2D(
        mpHitAlbedoAtlas,
        traceDim,
        ResourceFormat::RGBA16Float,
        "DDGI::HitAlbedoAtlas",
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );

    ensureTex2D(
        mpRadianceAtlas,
        radDim,
        ResourceFormat::RGBA16Float,
        "DDGI::RadianceAtlas",
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    ensureTex2D(
        mpIrradianceAtlas,
        irrDim,
        ResourceFormat::RGBA16Float,
        "DDGI::IrradianceAtlas",
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );

    mDirty &= ~DDGIDirtyFlags::Atlases;
}

void DDGIPass::prepareTraceProgram()
{
    if (!mpScene)
        return;

    ProgramDesc desc;
    desc.addShaderModules(mpScene->getShaderModules());
    desc.addShaderLibrary(kTraceGBufferShader);

    desc.setMaxPayloadSize(64);
    desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
    desc.setMaxTraceRecursionDepth(1);

    mpTraceSBT = RtBindingTable::create(1, 1, mpScene->getGeometryCount());
    const auto& sbt = mpTraceSBT;

    sbt->setRayGen(desc.addRayGen("rayGen"));
    sbt->setMiss(0, desc.addMiss("miss"));

    std::vector<GlobalGeometryID> all;

    for (const auto t : {Scene::GeometryType::TriangleMesh, Scene::GeometryType::DisplacedTriangleMesh, Scene::GeometryType::Curve})
    {
        if (mpScene->hasGeometryType(t))
        {
            auto ids = mpScene->getGeometryIDs(t);
            all.insert(all.end(), ids.begin(), ids.end());
        }
    }
    if (!all.empty())
        sbt->setHitGroup(0, all, desc.addHitGroup("closestHit", "anyHit"));

    mpTraceProgram = Program::create(mpDevice, desc, mpScene->getSceneDefines());
    mpTraceProgram->setTypeConformances(mpScene->getTypeConformances());

    mpTraceVars = RtProgramVars::create(mpDevice, mpTraceProgram, mpTraceSBT);

    mDirty &= ~DDGIDirtyFlags::RtPrograms;
}

void DDGIPass::prepareVizResources()
{
    mpProbeSphere = TriangleMesh::createSphere(mOpt.probeVizRadius, 16, 8);

    const auto& vertices = mpProbeSphere->getVertices();
    const auto& indices = mpProbeSphere->getIndices();

    const ref<Buffer> pVB = mpDevice->createBuffer(
        vertices.size() * sizeof(TriangleMesh::Vertex), ResourceBindFlags::Vertex, MemoryType::DeviceLocal, vertices.data()
    );
    const ref<Buffer> pIB =
        mpDevice->createBuffer(indices.size() * sizeof(uint32_t), ResourceBindFlags::Index, MemoryType::DeviceLocal, indices.data());

    ref<VertexLayout> pLayout = VertexLayout::create();
    const ref<VertexBufferLayout> pBufLayout = VertexBufferLayout::create();
    pBufLayout->addElement("POSITION", offsetof(TriangleMesh::Vertex, position), ResourceFormat::RGB32Float, 1, 0);
    pBufLayout->addElement("NORMAL", offsetof(TriangleMesh::Vertex, normal), ResourceFormat::RGB32Float, 1, 1);
    pBufLayout->addElement("TEXCOORD", offsetof(TriangleMesh::Vertex, texCoord), ResourceFormat::RG32Float, 1, 2);
    pLayout->addBufferLayout(0, pBufLayout);

    const Vao::BufferVec buffers = {pVB};
    mpProbeSphereVao = Vao::create(Vao::Topology::TriangleList, pLayout, buffers, pIB, ResourceFormat::R32Uint);

    mpVizProgram = Program::createGraphics(mpDevice, kVisualizeShader, "vsMain", "psMain");

    mpVizState = GraphicsState::create(mpDevice);
    mpVizState->setProgram(mpVizProgram);
    mpVizState->setVao(mpProbeSphereVao);

    DepthStencilState::Desc ds;
    ds.setDepthEnabled(true);
    ds.setDepthWriteMask(false);
    ds.setDepthFunc(ComparisonFunc::LessEqual);
    mpVizState->setDepthStencilState(DepthStencilState::create(ds));

    RasterizerState::Desc rs;
    rs.setCullMode(RasterizerState::CullMode::Back);
    mpVizState->setRasterizerState(RasterizerState::create(rs));

    mpVizVars = ProgramVars::create(mpDevice, mpVizProgram->getReflector());

    mDirty &= ~DDGIDirtyFlags::VizResources;
}

void DDGIPass::prepareBlendResources(const RenderData& rd)
{
    // Recreate blend program with scene defines so it can access gScene
    if (!mpBlendProgram || is_set(mDirty, DDGIDirtyFlags::BlendProgram))
    {
        DefineList defines;
        if (mpScene)
        {
            defines.add(mpScene->getSceneDefines());
        }

        ProgramDesc desc;
        if (mpScene)
        {
            desc.addShaderModules(mpScene->getShaderModules());
        }
        desc.addShaderLibrary(kBlendShader).vsEntry("vsMain").psEntry("psMain");
        if (mpScene)
        {
            desc.addTypeConformances(mpScene->getTypeConformances());
        }

        mpBlendProgram = Program::create(mpDevice, desc, defines);

        mpBlendState = GraphicsState::create(mpDevice);
        mpBlendState->setProgram(mpBlendProgram);

        ref<VertexLayout> pLayout = VertexLayout::create();
        mpBlendState->setVao(Vao::create(Vao::Topology::TriangleList, pLayout));

        mpBlendVars = ProgramVars::create(mpDevice, mpBlendProgram->getReflector());
        mDirty &= ~DDGIDirtyFlags::BlendProgram;
    }

    auto out = rd.getTexture(kColorOut);
    if (!out)
        return;

    if (!mpBlendFbo || mpBlendFbo->getColorTexture(0) != out)
    {
        mpBlendFbo = Fbo::create(mpDevice, {out});
    }
}

void DDGIPass::stageGenerateProbes(RenderContext* ctx)
{
    FALCOR_PROFILE(ctx, "DDGI::GenerateProbes");

    const auto var = mpGenerateProbesPass->getRootVar();
    var["DDGIConstants"]["gOrigin"] = mOpt.origin;
    var["DDGIConstants"]["gSpacing"] = mOpt.spacing;
    var["DDGIConstants"]["gProbeCounts"] = mOpt.probeCounts;
    var["gProbePositions"] = mpProbePositions;

    mpGenerateProbesPass->execute(ctx, mOpt.probeCounts.x, mOpt.probeCounts.y, mOpt.probeCounts.z);

    mDirty &= ~DDGIDirtyFlags::Probes;
}

void DDGIPass::stageTraceProbeGBuffer(RenderContext* ctx) const
{
    if (!mOpt.enableTrace)
        return;
    if (!mpScene || !mpTraceProgram || !mpTraceVars)
        return;

    FALCOR_PROFILE(ctx, "DDGI::Trace(GBuffer)");

    const auto var = mpTraceVars->getRootVar();

    mpScene->bindShaderData(var["gScene"]);

    var["DDGIConstants"]["gProbeCounts"] = mOpt.probeCounts;
    var["DDGIConstants"]["gTileRes"] = mOpt.tileResTrace;
    var["DDGIConstants"]["gMaxRayDistance"] = mOpt.maxRayDistance;

    var["gProbePositions"] = mpProbePositions;

    var["gHitPosAtlas"] = mpHitPosAtlas;
    var["gHitNormalAtlas"] = mpHitNormalAtlas;
    var["gHitAlbedoAtlas"] = mpHitAlbedoAtlas;

    const uint32_t totalProbes = getProbeCount();

    mpScene->raytrace(ctx, mpTraceProgram.get(), mpTraceVars, uint3(mOpt.tileResTrace, mOpt.tileResTrace, totalProbes));
}

void DDGIPass::stageComputeRadiance(RenderContext* ctx) const
{
    if (!mOpt.enableRadiance)
        return;

    FALCOR_PROFILE(ctx, "DDGI::Radiance");

    const auto var = mpRadiancePass->getRootVar();
    var["DDGIConstants"]["gTileResTrace"] = mOpt.tileResTrace;
    var["DDGIConstants"]["gTileResRadiance"] = mOpt.tileResRadiance;
    var["DDGIConstants"]["gProbeCounts"] = mOpt.probeCounts;

    var["PerFrameCB"]["gFrameCount"] = mFrameCount;

    var["gHitPosAtlas"] = mpHitPosAtlas;
    var["gHitNormalAtlas"] = mpHitNormalAtlas;
    var["gHitAlbedoAtlas"] = mpHitAlbedoAtlas;
    var["gRadianceAtlas"] = mpRadianceAtlas;

    mpSampleGenerator->bindShaderData(var);

    mpScene->bindShaderData(var["gScene"]);
    mpScene->bindShaderDataForRaytracing(ctx, var["gScene"]);

    const uint2 dim = getAtlasDims(mOpt.tileResRadiance);
    mpRadiancePass->execute(ctx, dim.x, dim.y, 1u);
}

void DDGIPass::stageComputeIrradiance(RenderContext* ctx) const
{
    if (!mOpt.enableIrradiance)
        return;

    FALCOR_PROFILE(ctx, "DDGI::Irradiance");

    auto var = mpIrradiancePass->getRootVar();
    var["DDGIConstants"]["gTileResRadiance"] = mOpt.tileResRadiance;
    var["DDGIConstants"]["gTileResIrradiance"] = mOpt.tileResIrradiance;
    var["DDGIConstants"]["gProbeCounts"] = mOpt.probeCounts;

    var["gRadianceAtlas"] = mpRadianceAtlas;
    var["gIrradianceAtlas"] = mpIrradianceAtlas;

    const uint2 dim = getAtlasDims(mOpt.tileResIrradiance);
    mpIrradiancePass->execute(ctx, dim.x, dim.y, 1u);
}

void DDGIPass::stageBlend(RenderContext* ctx, const RenderData& rd)
{
    if (!mOpt.enableBlend)
        return;

    FALCOR_PROFILE(ctx, "DDGI::Blend");

    auto out = rd.getTexture(kColorOut);
    if (!out)
        return;

    prepareBlendResources(rd);

    auto depthIn = rd.getTexture(kDepthIn);
    auto normalIn = rd.getTexture(kNormalIn);
    auto albedoIn = rd.getTexture(kAlbedoIn);
    auto emissiveIn = rd.getTexture(kEmissiveIn);

    if (!depthIn)
        return;

    mpBlendState->setFbo(mpBlendFbo);

    auto var = mpBlendVars->getRootVar();

    mpScene->bindShaderData(var["gScene"]);

    var["gDepthIn"] = depthIn;
    var["gNormalIn"] = normalIn;
    var["gAlbedoIn"] = albedoIn;
    var["gEmissiveIn"] = emissiveIn;

    var["gProbePositions"] = mpProbePositions;
    var["gIrradianceAtlas"] = mpIrradianceAtlas;

    if (!mpLinearSampler)
    {
        Sampler::Desc desc;
        desc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Linear);
        desc.setAddressingMode(TextureAddressingMode::Clamp, TextureAddressingMode::Clamp, TextureAddressingMode::Clamp);
        mpLinearSampler = mpDevice->createSampler(desc);
    }
    var["gSampler"] = mpLinearSampler;

    var["DDGIConstants"]["gOrigin"] = mOpt.origin;
    var["DDGIConstants"]["gSpacing"] = mOpt.spacing;
    var["DDGIConstants"]["gProbeCounts"] = mOpt.probeCounts;
    var["DDGIConstants"]["gTileResIrradiance"] = mOpt.tileResIrradiance;
    var["DDGIConstants"]["gGIIntensity"] = mOpt.giIntensity;

    const auto& cam = mpScene->getCamera();
    var["PerFrameCB"]["gInvViewProj"] = cam->getInvViewProjMatrix();
    var["PerFrameCB"]["gCameraPos"] = cam->getPosition();

    ctx->draw(mpBlendState.get(), mpBlendVars.get(), 3, 0);
}

void DDGIPass::stageVisualize(RenderContext* ctx, const RenderData& rd)
{
    if (!mOpt.visualizeProbes)
        return;
    if (!mpScene || !mpVizProgram || !mpProbeSphereVao)
        return;

    FALCOR_PROFILE(ctx, "DDGI::VisualizeProbes");

    auto out = rd.getTexture(kColorOut);
    if (!out)
        return;

    ref<Texture> depth = rd.getTexture(kDepthIn);
    if (!depth)
    {
        if (!mpVizDepth || mpVizDepth->getWidth() != out->getWidth() || mpVizDepth->getHeight() != out->getHeight())
        {
            mpVizDepth = mpDevice->createTexture2D(
                out->getWidth(), out->getHeight(), ResourceFormat::D32Float, 1, 1, nullptr, ResourceBindFlags::DepthStencil
            );
            mpVizDepth->setName("DDGI::VizDepth");
        }
        depth = mpVizDepth;

        ctx->clearDsv(depth->getDSV().get(), 1.0f, 0);
    }

    if (!mpVizFbo || mpVizFboColor != out || mpVizFboDepth != depth)
    {
        mpVizFbo = Fbo::create(mpDevice, {out}, depth);
        mpVizFboColor = out;
        mpVizFboDepth = depth;
    }

    mpVizState->setFbo(mpVizFbo);

    const auto var = mpVizVars->getRootVar();
    const auto& cam = mpScene->getCamera();
    var["PerFrameCB"]["gViewProj"] = cam->getViewProjMatrix();
    var["PerFrameCB"]["gProbeColor"] = mOpt.probeVizColor;
    var["PerFrameCB"]["gProbeRadius"] = mOpt.probeVizRadius;
    var["PerFrameCB"]["gCameraPos"] = cam->getPosition();

    var["gProbePositions"] = mpProbePositions;

    const uint32_t indexCount = static_cast<uint32_t>(mpProbeSphere->getIndices().size());
    const uint32_t instCount = getProbeCount();

    ctx->drawIndexedInstanced(mpVizState.get(), mpVizVars.get(), indexCount, instCount, 0, 0, 0);
}

void DDGIPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto& dict = renderData.getDictionary();
    if (mOptionsChanged)
    {
        const auto flags = dict.getValue(kRenderPassRefreshFlags, RenderPassRefreshFlags::None);
        dict[kRenderPassRefreshFlags] = flags | RenderPassRefreshFlags::RenderOptionsChanged;
        mOptionsChanged = false;
    }

    if (!mpScene)
        return;

    rebuildIfNeeded(pRenderContext);

    if (is_set(mDirty, DDGIDirtyFlags::Probes))
        stageGenerateProbes(pRenderContext);

    stageTraceProbeGBuffer(pRenderContext);
    stageComputeRadiance(pRenderContext);
    stageComputeIrradiance(pRenderContext);

    if (mOpt.enableBlend)
    {
        stageBlend(pRenderContext, renderData);
    }
    else
    {
        if (const auto colorOut = renderData.getTexture(kColorOut))
        {
            pRenderContext->clearRtv(colorOut->getRTV().get(), float4(0.f, 0.f, 0.f, 1.f));
        }
    }

    stageVisualize(pRenderContext, renderData);

    mFrameCount++;
}

void DDGIPass::renderUI(Gui::Widgets& widget)
{
    bool dirtyProbes = false;
    bool dirtyAtlases = false;
    bool dirtyViz = false;

    widget.text("DDGI Pipeline");
    widget.separator();

    widget.checkbox("Visualize Probes", mOpt.visualizeProbes);
    dirtyViz |= widget.var("Probe Viz Radius (world)", mOpt.probeVizRadius, 0.001f, 10.f, 0.001f);
    widget.rgbColor("Probe Viz Color", mOpt.probeVizColor);

    widget.separator();

    dirtyProbes |= widget.var("Origin", mOpt.origin, -10000.f, 10000.f, 0.1f);
    dirtyProbes |= widget.var("Spacing", mOpt.spacing, 0.001f, 1000.f, 0.01f);

    if (auto counts = int3(mOpt.probeCounts); widget.var("Probe Counts", counts, 1, 128))
    {
        mOpt.probeCounts = uint3(counts);
        dirtyProbes = true;
        dirtyAtlases = true;
    }

    widget.text("Total Probes: " + std::to_string(getProbeCount()));
    widget.separator();

    dirtyAtlases |= widget.var("TileRes Trace", mOpt.tileResTrace, 4u, 64u);
    dirtyAtlases |= widget.var("TileRes Radiance", mOpt.tileResRadiance, 4u, 64u);
    dirtyAtlases |= widget.var("TileRes Irradiance", mOpt.tileResIrradiance, 2u, 32u);

    widget.var("Max Ray Distance", mOpt.maxRayDistance, 1.f, 1e6f, 1.f);
    widget.var("GI Intensity", mOpt.giIntensity, 0.f, 10.f, 0.01f);

    widget.separator();
    widget.text("Stages");
    widget.checkbox("Enable Trace", mOpt.enableTrace);
    widget.checkbox("Enable Radiance", mOpt.enableRadiance);
    widget.checkbox("Enable Irradiance", mOpt.enableIrradiance);
    widget.checkbox("Enable Blend", mOpt.enableBlend);

    if (dirtyProbes)
    {
        mDirty |= DDGIDirtyFlags::Probes;
        mOptionsChanged = true;
    }
    if (dirtyAtlases)
    {
        mDirty |= DDGIDirtyFlags::Atlases;
        mOptionsChanged = true;
    }
    if (dirtyViz)
    {
        mDirty |= DDGIDirtyFlags::VizResources;
        mOptionsChanged = true;
    }
}
