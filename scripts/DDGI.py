from falcor import *

def render_graph_DDGI():
    g = RenderGraph("DDGI")

    GBuffer = createPass("GBufferRaster", {
        "outputSize": "Default",
        "samplePattern": "Center",
        "sampleCount": 1
    })
    g.addPass(GBuffer, "GBuffer")

    DDGI = createPass("DDGIPass", {
        "visualizeProbes": True
    })
    g.addPass(DDGI, "DDGI")

    AccumulatePass = createPass("AccumulatePass", {
        "enabled": True,
        "precisionMode": "Single"
    })
    g.addPass(AccumulatePass, "AccumulatePass")

    ToneMapper = createPass("ToneMapper", {
        "autoExposure": False,
        "exposureCompensation": 0.0
    })
    g.addPass(ToneMapper, "ToneMapper")

    g.addEdge("GBuffer.depth", "DDGI.depthIn")
    g.addEdge("GBuffer.normW", "DDGI.normalIn")
    g.addEdge("GBuffer.diffuseOpacity", "DDGI.albedoIn")
    g.addEdge("GBuffer.emissive", "DDGI.emissiveIn")

    g.addEdge("DDGI.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")

    return g

m.addGraph(render_graph_DDGI())
