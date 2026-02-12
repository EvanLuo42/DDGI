from falcor import *

def render_graph_DDGIVisualization():
    g = RenderGraph("DDGIVisualization")

    DDGIPass = createPass("DDGIPass", {})
    g.addPass(DDGIPass, "DDGIPass")

    g.markOutput("DDGIPass.color")

    return g

DDGIVisualization = render_graph_DDGIVisualization()
try: m.addGraph(DDGIVisualization)
except NameError: None
