
function Gen-Spv {
    param (
        [string]$ShaderName
    )
    Start-Process -Wait -NoNewWindow -FilePath "glsLangValidator" -ArgumentList "-V100","--target-env vulkan1.2","-o $ShaderName.spv","$ShaderName"
}

Gen-Spv deferred.vert
Gen-Spv deferred.frag
Gen-Spv offscreen.vert
Gen-Spv offscreen.frag
Gen-Spv prep_indirect.comp
