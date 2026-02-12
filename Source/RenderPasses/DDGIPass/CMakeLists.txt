add_plugin(DDGIPass)

target_sources(DDGIPass PRIVATE
    DDGIPass.cpp
    DDGIPass.h
)

target_copy_shaders(DDGIPass RenderPasses/DDGIPass)

target_source_group(DDGIPass "RenderPasses")
