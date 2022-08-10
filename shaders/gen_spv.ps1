
function Gen-Spv {
    param (
        [string]$ShaderName
    )
    Start-Process -Wait -NoNewWindow -FilePath "glsLangValidator" -ArgumentList "-V100","--target-env vulkan1.2","-o $ShaderName.spv","$ShaderName"
}

Gen-Spv offscreen.vert
Gen-Spv offscreen.frag
Gen-Spv deferred.comp
Gen-Spv prep_indirect.comp
