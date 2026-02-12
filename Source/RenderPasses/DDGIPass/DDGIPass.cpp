#include "DDGIPass.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include <vector>

namespace
{
const std::string kGenerateProbesShader = "RenderPasses/DDGIPass/GenerateProbes.cs.slang";
const std::string kRTRadianceShader = "RenderPasses/DDGIPass/RTRadiance.rt.slang";
const std::string kVisualizeProbesShader = "RenderPasses/DDGIPass/VisualizeProbe.ps.slang";

const char kOrigin[] = "origin";
const char kSpacing[] = "spacing";
const char kProbeCounts[] = "probeCounts";
const char kVisualizeProbes[] = "visualizeProbes";
const char kProbeRadius[] = "probeRadius";
const char kProbeColor[] = "probeColor";
const char kRayPerProbe[] = "rayPerProbe";
const char kProbeMaxRayDistance[] = "probeMaxRayDistance";

const std::string kColorOutput = "color";

const ChannelList kOutputChannels = {
    {kColorOutput, "gOutputColor", "Output color", false, ResourceFormat::RGBA32Float},
};
} // namespace

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DDGIPass>();
}

DDGIPass::DDGIPass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    parseProperties(props);

    // Create compute pass for generating probe positions
    mpGenerateProbesPass = ComputePass::create(mpDevice, kGenerateProbesShader, "main", DefineList(), true);
}

void DDGIPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kOrigin)
            mOrigin = value;
        else if (key == kSpacing)
            mSpacing = value;
        else if (key == kProbeCounts)
            mProbeCounts = value;
        else if (key == kVisualizeProbes)
            mVisualizeProbes = value;
        else if (key == kProbeRadius)
            mProbeSphereRadius = value;
        else if (key == kProbeColor)
            mProbeColor = value;
        else if (key == kRayPerProbe)
            mRaysPerProbe = value;
        else if (key == kProbeMaxRayDistance)
            probeMaxRayDistance = value;
        else
            logWarning("Unknown property '{}' in DDGIPass properties.", key);
    }
}

Properties DDGIPass::getProperties() const
{
    Properties props;
    props[kOrigin] = mOrigin;
    props[kSpacing] = mSpacing;
    props[kProbeCounts] = mProbeCounts;
    props[kVisualizeProbes] = mVisualizeProbes;
    props[kProbeRadius] = mProbeSphereRadius;
    props[kProbeColor] = mProbeColor;
    props[kRayPerProbe] = mRaysPerProbe;
    props[kProbeMaxRayDistance] = probeMaxRayDistance;
    return props;
}

RenderPassReflection DDGIPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    addRenderPassOutputs(reflector, kOutputChannels, ResourceBindFlags::RenderTarget);

    return reflector;
}

void DDGIPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;

    mpVisualizeProgram = nullptr;
    mpVisualizeState = nullptr;
    mpVisualizeVars = nullptr;
    mpProbeSphereVao = nullptr;

    if (mpScene)
    {
        AABB sceneBounds = mpScene->getSceneBounds();
        mOrigin = sceneBounds.minPoint;

        float3 sceneExtent = sceneBounds.extent();
        mSpacing = sceneExtent / float3(mProbeCounts);

        mProbesNeedUpdate = true;
    }
}

void DDGIPass::prepareProbeBuffer()
{
    if (uint32_t probeCount = mProbeCounts.x * mProbeCounts.y * mProbeCounts.z;
        !mpProbePositionsBuffer || mpProbePositionsBuffer->getElementCount() < probeCount)
    {
        mpProbePositionsBuffer = mpDevice->createStructuredBuffer(
            sizeof(float3),
            probeCount,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            MemoryType::DeviceLocal,
            nullptr,
            false
        );
        mpProbePositionsBuffer->setName("DDGIPass::ProbePositions");
    }
}

void DDGIPass::generateProbes(RenderContext* pRenderContext)
{
    FALCOR_PROFILE(pRenderContext, "GenerateProbes");

    prepareProbeBuffer();

    auto var = mpGenerateProbesPass->getRootVar();
    var["DDGIConstants"]["gOrigin"] = mOrigin;
    var["DDGIConstants"]["gSpacing"] = mSpacing;
    var["DDGIConstants"]["gProbeCounts"] = mProbeCounts;
    var["gProbePositions"] = mpProbePositionsBuffer;

    mpGenerateProbesPass->execute(pRenderContext, mProbeCounts.x, mProbeCounts.y, mProbeCounts.z);

    mProbesNeedUpdate = false;
}

void DDGIPass::prepareProbeVisualization()
{
    if (mpVisualizeProgram && mpProbeSphereVao)
        return;

    mpProbeSphere = TriangleMesh::createSphere(mProbeSphereRadius, 16, 8);

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

    Vao::BufferVec buffers = {pVB};
    mpProbeSphereVao = Vao::create(Vao::Topology::TriangleList, pLayout, buffers, pIB, ResourceFormat::R32Uint);

    mpVisualizeProgram = Program::createGraphics(mpDevice, kVisualizeProbesShader, "vsMain", "psMain");

    mpVisualizeState = GraphicsState::create(mpDevice);
    mpVisualizeState->setProgram(mpVisualizeProgram);

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(true);
    dsDesc.setDepthWriteMask(true);
    dsDesc.setDepthFunc(ComparisonFunc::Less);
    mpVisualizeState->setDepthStencilState(DepthStencilState::create(dsDesc));

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::Back);
    mpVisualizeState->setRasterizerState(RasterizerState::create(rsDesc));

    mpVisualizeVars = ProgramVars::create(mpDevice, mpVisualizeProgram->getReflector());
}

void DDGIPass::visualizeProbes(RenderContext* pRenderContext, const RenderData& renderData)
{
    FALCOR_PROFILE(pRenderContext, "VisualizeProbes");

    if (!mpScene)
        return;

    prepareProbeVisualization();

    if (!mpProbeSphereVao || !mpVisualizeProgram)
        return;

    auto pColorOutput = renderData.getTexture(kColorOutput);
    if (!pColorOutput)
        return;

    ref<Texture> pDepthBuffer = mpDevice->createTexture2D(
        pColorOutput->getWidth(), pColorOutput->getHeight(), ResourceFormat::D32Float, 1, 1, nullptr, ResourceBindFlags::DepthStencil
    );

    const ref<Fbo> pFbo = Fbo::create(mpDevice, {pColorOutput}, pDepthBuffer);

    pRenderContext->clearFbo(pFbo.get(), float4(0.1f, 0.1f, 0.1f, 1.0f), 1.0f, 0, FboAttachmentType::All);

    mpVisualizeState->setFbo(pFbo);
    mpVisualizeState->setVao(mpProbeSphereVao);

    const auto var = mpVisualizeVars->getRootVar();

    const auto& pCamera = mpScene->getCamera();
    var["PerFrameCB"]["gViewProj"] = pCamera->getViewProjMatrix();
    var["PerFrameCB"]["gCameraPos"] = pCamera->getPosition();
    var["PerFrameCB"]["gProbeRadius"] = mProbeSphereRadius;
    var["PerFrameCB"]["gProbeColor"] = mProbeColor;

    var["gProbePositions"] = mpProbePositionsBuffer;

    uint32_t probeCount = mProbeCounts.x * mProbeCounts.y * mProbeCounts.z;
    uint32_t indexCount = static_cast<uint32_t>(mpProbeSphere->getIndices().size());

    pRenderContext->drawIndexedInstanced(mpVisualizeState.get(), mpVisualizeVars.get(), indexCount, probeCount, 0, 0, 0);
}

void DDGIPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto& dict = renderData.getDictionary();
    if (mOptionsChanged)
    {
        const auto flags = dict.getValue(kRenderPassRefreshFlags, RenderPassRefreshFlags::None);
        dict[kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        mOptionsChanged = false;
    }

    if (!mpScene)
        return;

    if (mProbesNeedUpdate)
    {
        generateProbes(pRenderContext);
    }

    if (mVisualizeProbes)
    {
        visualizeProbes(pRenderContext, renderData);
    }
}

void DDGIPass::renderUI(Gui::Widgets& widget)
{
    bool dirty = false;

    dirty |= widget.checkbox("Visualize Probes", mVisualizeProbes);
    widget.tooltip("Toggle probe visualization.", true);

    dirty |= widget.var("Probe Radius", mProbeSphereRadius, 0.01f, 10.0f, 0.01f);
    widget.tooltip("Radius of each probe.", true);

    dirty |= widget.rgbColor("Probe Color", mProbeColor, true);
    widget.tooltip("Probe visualization color.", true);

    widget.separator();

    dirty |= widget.var("Origin", mOrigin, -1000.0f, 1000.0f, 0.1f);
    widget.tooltip("Origin of the probe grid volume.", true);

    dirty |= widget.var("Spacing", mSpacing, 0.01f, 100.0f, 0.1f);
    widget.tooltip("Spacing between probes in each dimension.", true);

    int3 probeCounts = int3(mProbeCounts);
    if (widget.var("Probe Counts", probeCounts, 1, 128))
    {
        mProbeCounts = uint3(probeCounts);
        dirty = true;
    }
    widget.tooltip("Number of probes in each dimension.", true);

    const uint32_t totalProbes = mProbeCounts.x * mProbeCounts.y * mProbeCounts.z;
    widget.text("Total Probes: " + std::to_string(totalProbes));

    if (dirty)
    {
        mProbesNeedUpdate = true;
        mOptionsChanged = true;
    }
}
