add_plugin(DDGIPass)

target_sources(DDGIPass PRIVATE
    DDGIPass.cpp
    DDGIPass.h
    ComputeIrradiance.cs.slang
    ComputeRadiance.cs.slang
    GenerateProbes.cs.slang
    TraceProbeGBuffer.rt.slang
)

target_copy_shaders(DDGIPass RenderPasses/DDGIPass)

target_source_group(DDGIPass "RenderPasses")
