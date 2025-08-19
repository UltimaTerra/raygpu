#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tint_c_api.h>
const char* stageNames[16] = {
    "WGPUShaderStageEnum_Vertex",
    "WGPUShaderStageEnum_Fragment",
    "WGPUShaderStageEnum_Compute",
    "WGPUShaderStageEnum_TessControl",
    "WGPUShaderStageEnum_TessEvaluation",
    "WGPUShaderStageEnum_Geometry",
    "WGPUShaderStageEnum_RayGen",
    "WGPUShaderStageEnum_Intersect",
    "WGPUShaderStageEnum_AnyHit",
    "WGPUShaderStageEnum_ClosestHit",
    "WGPUShaderStageEnum_Miss",
    "WGPUShaderStageEnum_Callable",
    "WGPUShaderStageEnum_Task",
    "WGPUShaderStageEnum_Mesh",
    "<not a stage>",
    "<not a stage>",
};
size_t append_to_filename_and_replace_ending(const char* filename, const char* suffix, const char* new_ext, char* newname) {
    if (!filename || !suffix || !new_ext || !newname) {
        return 0;
    }
    const char* dot = strrchr(filename, '.');
    size_t base_len;
    if (dot != NULL) {
        base_len = dot - filename;
    } else {
        base_len = strlen(filename);
    }
    strncpy(newname, filename, base_len);
    newname[base_len] = '\0';
    strcat(newname, suffix);
    strcat(newname, new_ext);
    return strlen(newname);
}


int main(int argc, char** argv){
    if(argc != 2){
        fprintf(stderr, "Usage: %s <filename.wgsl>\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];
    FILE* f = fopen(argv[1], "r");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = calloc(1, size + 1);
    size_t read = fread(buffer, 1, size, f);
    buffer[read] = '\0'; //not actually required because source.code will be a WGPUStringView

    WGPUShaderSourceWGSL source = {
        .chain.sType = WGPUSType_ShaderSourceWGSL,
        .code = {
            .data = buffer,
            .length = size
        }
    };
    tc_SpirvBlob spirv = wgslToSpirv(&source, 0, NULL);
    for(uint32_t i = 0;i < 16;i++){
        char newFilename[1024] = {0};
        if(spirv.entryPoints[i].codeSize){
            size_t fnameLength = append_to_filename_and_replace_ending(filename, stageNames[i], ".spv", newFilename);
            FILE* outputfile = fopen(newFilename, "w");
            fwrite((char*)spirv.entryPoints[i].code, 1, spirv.entryPoints[i].codeSize, outputfile);
            fclose(outputfile);
        }
    }
}
