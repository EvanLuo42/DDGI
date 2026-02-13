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

    g.addEdge("GBuffer.depth", "DDGI.depthIn")
    g.addEdge("GBuffer.diffuseOpacity", "DDGI.colorIn")

    g.markOutput("DDGI.color")

    return g

m.addGraph(render_graph_DDGI())