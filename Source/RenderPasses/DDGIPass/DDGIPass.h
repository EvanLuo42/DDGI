/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
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

class DDGIPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(DDGIPass, "DDGIPass", "Standalone pass for specular lighting using DDGI.");

    static ref<DDGIPass> create(ref<Device> pDevice, const Properties& props) { return make_ref<DDGIPass>(pDevice, props); }

    DDGIPass(ref<Device> pDevice, const Properties& props);

    Properties getProperties() const override;
    RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    void renderUI(Gui::Widgets& widget) override;
    void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

private:
    void parseProperties(const Properties& props);

    void generateProbes(RenderContext* pRenderContext);
    void prepareProbeBuffer();

    void visualizeProbes(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareProbeVisualization();

    ref<Scene> mpScene;

    ref<ComputePass> mpGenerateProbesPass;
    ref<Program> mpVisualizeProgram;
    ref<ProgramVars> mpVisualizeVars;
    ref<GraphicsState> mpVisualizeState;

    ref<Buffer> mpProbePositionsBuffer;
    ref<TriangleMesh> mpProbeSphere;
    ref<Vao> mpProbeSphereVao;

    // Ray tracing radiance options
    uint32_t mRaysPerProbe = 288;
    float probeMaxRayDistance = 100000.0f;

    // Volume probe grid options
    float3 mOrigin = float3(0.0f);
    float3 mSpacing = float3(1.0f);
    uint3 mProbeCounts = uint3(8, 8, 8);

    // Visualization options
    bool mVisualizeProbes = true;
    float mProbeSphereRadius = 0.5f;
    float3 mProbeColor = float3(1.0f, 1.0f, 1.0f);

    bool mProbesNeedUpdate = true;
    bool mOptionsChanged = false;
};
