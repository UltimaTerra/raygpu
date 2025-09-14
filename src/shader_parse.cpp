/*
 * MIT License
 * 
 * Copyright (c) 2025 @manuel5975p
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


//#include <ctre.hpp>


#include <raygpu.h>
#include <internals.hpp>
#include <vector>
#include <sstream>

#include <string_view>

#if SUPPORT_CUSTOM_WGSL_PARSER == 0
#include "simple_wgsl/wgsl_parser.h"
#include "simple_wgsl/wgsl_resolve.h"

inline bool str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    const size_t n = std::strlen(prefix);
    return std::strncmp(s, prefix, n) == 0;
}

inline WGPUShaderStageEnum to_stage_enum(WgslStage st) {
    switch (st) {
        case WGSL_STAGE_VERTEX:   return WGPUShaderStageEnum_Vertex;
        case WGSL_STAGE_FRAGMENT: return WGPUShaderStageEnum_Fragment;
        case WGSL_STAGE_COMPUTE:  return WGPUShaderStageEnum_Compute;
        default:                  return WGPUShaderStageEnum_Vertex; // safe default
    }
}

inline WGPUVertexFormat vf_from_numeric(int comps, WgslNumericType nt) {
    // Bool is not valid as a vertex attribute. Map to Uint32* as the least-bad fallback.
    switch (nt) {
        case WGSL_NUM_F32:
            switch (comps) {
                case 1: return WGPUVertexFormat_Float32;
                case 2: return WGPUVertexFormat_Float32x2;
                case 3: return WGPUVertexFormat_Float32x3;
                case 4: return WGPUVertexFormat_Float32x4;
            }
            break;
        case WGSL_NUM_I32:
            switch (comps) {
                case 1: return WGPUVertexFormat_Sint32;
                case 2: return WGPUVertexFormat_Sint32x2;
                case 3: return WGPUVertexFormat_Sint32x3;
                case 4: return WGPUVertexFormat_Sint32x4;
            }
            break;
        case WGSL_NUM_U32:
        case WGSL_NUM_BOOL: // fallback
            switch (comps) {
                case 1: return WGPUVertexFormat_Uint32;
                case 2: return WGPUVertexFormat_Uint32x2;
                case 3: return WGPUVertexFormat_Uint32x3;
                case 4: return WGPUVertexFormat_Uint32x4;
            }
            break;
        default:
            break;
    }
#ifdef WGPUVertexFormat_Undefined
    return WGPUVertexFormat_Undefined;
#else
    return WGPUVertexFormat_Float32;
#endif
}

inline format_or_sample_type parse_format_token(const char* tkn) {
    if (!tkn) return we_dont_know;
    if (std::strcmp(tkn, "r32float") == 0)   return format_r32float;
    if (std::strcmp(tkn, "r32uint") == 0)    return format_r32uint;
    if (std::strcmp(tkn, "rgba8unorm") == 0) return format_rgba8unorm;
    if (std::strcmp(tkn, "rgba32float") == 0)return format_rgba32float;
    if (std::strcmp(tkn, "f32") == 0)        return sample_f32;
    if (std::strcmp(tkn, "u32") == 0)        return sample_u32;
    return we_dont_know;
}

inline access_type parse_access_token(const char* tkn) {
    if (!tkn) return readwrite; // safest default
    if (std::strcmp(tkn, "read") == 0)       return readonly;
    if (std::strcmp(tkn, "write") == 0)      return writeonly;
    if (std::strcmp(tkn, "read_write") == 0) return readwrite;
    return readwrite;
}

struct ParsedTextureMeta {
    // Interpreted from a TypeNode like: texture_2d<T> or texture_storage_2d<F, access>
    bool is_storage = false;
    bool is_array = false;
    bool is_3d = false;
    format_or_sample_type fmt_or_sample = we_dont_know;
    access_type access = readwrite;
};

inline ParsedTextureMeta parse_texture_typenode(const WgslAstNode* T) {
    ParsedTextureMeta m{};
    if (!T || T->type != WGSL_NODE_TYPE) return m;
    const char* name = T->type_node.name ? T->type_node.name : "";

    m.is_storage = str_starts_with(name, "texture_storage_");
    m.is_array   = str_starts_with(name, "texture_2d_array") || str_starts_with(name, "texture_storage_2d_array");
    m.is_3d      = str_starts_with(name, "texture_3d") || str_starts_with(name, "texture_storage_3d");

    // The simple parser places template identifiers into type_args.
    // For storage textures: <FORMAT, ACCESS> ; for sampled textures: <SAMPLE_T>
    for (int i = 0; i < T->type_node.type_arg_count; ++i) {
        const WgslAstNode* a = T->type_node.type_args[i];
        if (a && a->type == WGSL_NODE_TYPE && a->type_node.name) {
            format_or_sample_type f = parse_format_token(a->type_node.name);
            if (f != we_dont_know) {
                m.fmt_or_sample = f;
                continue;
            }
            // Maybe it's an access token
            m.access = parse_access_token(a->type_node.name);
        }
    }
    return m;
}

std::vector<std::pair<WGPUShaderStageEnum, std::string>> getEntryPointsWGSL_Simple(const char* shaderSourceWGSL) {
    std::vector<std::pair<WGPUShaderStageEnum, std::string>> eps;

    if (!shaderSourceWGSL) return eps;

    WgslAstNode* ast = wgsl_parse(shaderSourceWGSL);
    if (!ast) return eps;

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) { wgsl_free_ast(ast); return eps; }

    int n = 0;
    const WgslResolverEntrypoint* arr = wgsl_resolver_entrypoints(R, &n);
    for (int i = 0; i < n; ++i) {
        WGPUShaderStageEnum st = to_stage_enum(arr[i].stage);
        eps.emplace_back(st, arr[i].name ? arr[i].name : "");
    }

    wgsl_resolve_free((void*)arr);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return eps;
}

InOutAttributeInfo getAttributesWGSL_Simple(ShaderSources sources) {
    InOutAttributeInfo info{}; // zero-init
    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr) return info;

    const char* src = (const char*)sources.sources[0].data;

    WgslAstNode* ast = wgsl_parse(src);
    if (!ast) return info;

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) { wgsl_free_ast(ast); return info; }

    // Choose first vertex entrypoint
    int ep_count = 0;
    const WgslResolverEntrypoint* eps = wgsl_resolver_entrypoints(R, &ep_count);

    const char* vertex_name = nullptr;
    for (int i = 0; i < ep_count; ++i) {
        if (eps[i].stage == WGSL_STAGE_VERTEX) { vertex_name = eps[i].name; break; }
    }

    if (vertex_name) {
        WgslVertexSlot* slots = nullptr;
        int slot_count = wgsl_resolver_vertex_inputs(R, vertex_name, &slots);
        // Clamp to struct capacity if needed
        info.vertexAttributeCount = (uint32_t)slot_count;
        for (int i = 0; i < slot_count; ++i) {
            ReflectionVertexAttribute* out = &info.vertexAttributes[i];
            out->location = (uint32_t)slots[i].location;
            out->format = vf_from_numeric(slots[i].component_count, slots[i].numeric_type);
            // Name is optional here; leave empty.
            out->name[0] = '\0';
        }
        wgsl_resolve_free(slots);
    }

    // Fragment outputs not provided by the simple resolver. Leave attachmentCount = 0.
    wgsl_resolve_free((void*)eps);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return info;
}

std::unordered_map<std::string, ResourceTypeDescriptor> getBindingsWGSL_Simple(ShaderSources sources) {
    std::unordered_map<std::string, ResourceTypeDescriptor> out;

    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr) return out;
    const char* src = (const char*)sources.sources[0].data;

    WgslAstNode* ast = wgsl_parse(src);
    if (!ast) return out;

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) { wgsl_free_ast(ast); return out; }

    int n = 0;
    const WgslSymbolInfo* syms = wgsl_resolver_binding_vars(R, &n);
    for (int i = 0; i < n; ++i) {
        const WgslSymbolInfo& s = syms[i];
        if (!s.name || !s.decl_node || s.decl_node->type != WGSL_NODE_GLOBAL_VAR) continue;

        const GlobalVar& gv = s.decl_node->global_var;

        ResourceTypeDescriptor desc{};
        desc.location = (uint32_t)(s.has_binding ? s.binding_index : 0);
        if (s.has_min_binding_size) desc.minBindingSize = (uint32_t)s.min_binding_size;

        // Texture / sampler detection from the declared type
        const WgslAstNode* T = gv.type;
        const char* tname = (T && T->type == WGSL_NODE_TYPE) ? T->type_node.name : nullptr;

        bool handled = false;
        if (tname) {
            if (str_starts_with(tname, "texture_")) {
                ParsedTextureMeta meta = parse_texture_typenode(T);
                if (meta.is_storage) {
                    if (meta.is_array) {
                        desc.type = storage_texture2d_array;
                    } else if (meta.is_3d) {
                        desc.type = storage_texture3d;
                    } else {
                        desc.type = storage_texture2d;
                    }
                    desc.fstype = meta.fmt_or_sample;
                    desc.access = meta.access;
                } else {
                    if (meta.is_array) {
                        desc.type = texture2d_array;
                    } else if (meta.is_3d) {
                        desc.type = texture3d;
                    } else {
                        desc.type = texture2d;
                    }
                    desc.fstype = meta.fmt_or_sample; // sample type for sampled textures
                }
                handled = true;
            } else if (std::strcmp(tname, "sampler") == 0 || str_starts_with(tname, "sampler")) {
                desc.type = texture_sampler;
                handled = true;
            }
        }

        if (!handled) {
            // Buffers: infer from address space
            if (gv.address_space && std::strcmp(gv.address_space, "uniform") == 0) {
                desc.type = uniform_buffer;
            } else if (gv.address_space && std::strcmp(gv.address_space, "storage") == 0) {
                desc.type = storage_buffer;
                // Access for buffers is expressed in var<storage, read|read_write>.
                // The simple parser doesn't keep access here, so default to readwrite.
                desc.access = readwrite;
            } else {
                // Private/workgroup not bindable; skip.
                continue;
            }
        }

        out.emplace(s.name, desc);
    }

    wgsl_resolve_free((void*)syms);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return out;
}

// ---- Public surface ----
// If Tint is not enabled, export the expected symbols with the Simple implementation.
#if !defined(SUPPORT_WGSL_PARSER) || SUPPORT_WGSL_PARSER == 0

std::vector<std::pair<WGPUShaderStageEnum, std::string>> getEntryPointsWGSL(const char* shaderSourceWGSL) {
    return getEntryPointsWGSL_Simple(shaderSourceWGSL);
}

InOutAttributeInfo getAttributesWGSL(ShaderSources sources) {
    return getAttributesWGSL_Simple(sources);
}

std::unordered_map<std::string, ResourceTypeDescriptor> getBindingsWGSL(ShaderSources sources) {
    return getBindingsWGSL_Simple(sources);
}

#endif



#if defined(SUPPORT_WGSL_PARSER) && SUPPORT_WGSL_PARSER == 1
#include "src/tint/utils/result.h"
#include "src/tint/lang/spirv/writer/helpers/generate_bindings.h"
#include "src/tint/lang/spirv/writer/raise/raise.h"
#include <tint/tint.h>
#include "src/tint/lang/wgsl/ast/override.h"
#include "src/tint/lang/wgsl/ast/var.h"
#include "src/tint/lang/wgsl/program/program_builder.h"
#include "src/tint/lang/wgsl/ast/const.h"
#include "src/tint/lang/wgsl/ast/templated_identifier.h"
#include "src/tint/lang/wgsl/ast/module.h"
#include "src/tint/lang/wgsl/ast/identifier_expression.h"
#include "src/tint/lang/core/type/reference.h"
//#include <tint/lang/wgsl/reader/parser/parser.h>
//#include <tint/lang/wgsl/reader/reader.h>
//#include <tint/lang/core/type/reference.h>
//#include <tint/lang/spirv/writer/writer.h>
#endif

//#include <tint/lang/glsl/writer/writer.h>
//#include <tint/lang/wgsl/reader/reader.h>

const std::unordered_map<std::string, WGPUVertexFormat> builtins = [](){
    std::unordered_map<std::string, WGPUVertexFormat> map;
    map["vec2u"] = WGPUVertexFormat_Uint32x2;
    map["vec3u"] = WGPUVertexFormat_Uint32x3;
    map["vec4u"] = WGPUVertexFormat_Uint32x4;
    map["vec2f"] = WGPUVertexFormat_Float32x2;
    map["vec3f"] = WGPUVertexFormat_Float32x3;
    map["vec4f"] = WGPUVertexFormat_Float32x4;
    return map;
}();
const std::unordered_map<std::string, std::unordered_map<std::string, WGPUVertexFormat>> builtins_templated = [](){
    std::unordered_map<std::string, std::unordered_map<std::string, WGPUVertexFormat>> map;
    map["vec2"]["f32"] = WGPUVertexFormat_Float32x2;
    map["vec3"]["f32"] = WGPUVertexFormat_Float32x3;
    map["vec4"]["f32"] = WGPUVertexFormat_Float32x4;
    map["vec2"]["u32"] = WGPUVertexFormat_Uint32x2;
    map["vec3"]["u32"] = WGPUVertexFormat_Uint32x3;
    map["vec4"]["u32"] = WGPUVertexFormat_Uint32x4;
    return map;
}();
#if SUPPORT_WGSL_PARSER == 1
DescribedShaderModule LoadShaderModuleWGSL(ShaderSources sources) {
    
    DescribedShaderModule ret zeroinit;
    #if SUPPORT_WGPU_BACKEND == 1 || SUPPORT_WGPU_BACKEND == 0

    rassert(sources.language == sourceTypeWGSL, "Source language must be wgsl for this function");
    
    for(uint32_t i = 0;i < sources.sourceCount;i++){
        WGPUShaderModuleDescriptor mDesc zeroinit;
        WGPUShaderSourceWGSL source zeroinit;
        mDesc.nextInChain = &source.chain;
        source.chain.sType = WGPUSType_ShaderSourceWGSL;

        source.code = WGPUStringView{.data = (const char*)sources.sources[i].data, .length = sources.sources[i].sizeInBytes};
        WGPUShaderModule module = wgpuDeviceCreateShaderModule((WGPUDevice)GetDevice(), &mDesc);
        WGPUShaderStage sourceStageMask = sources.sources[i].stageMask;
        
        for(uint32_t i = 0;i < WGPUShaderStageEnum_EnumCount;++i){
            if(uint32_t(sourceStageMask) & (1u << i)){
                ret.stages[i].module = module;
            }
        }
        
        std::vector<std::pair<WGPUShaderStageEnum, std::string>> entryPoints = getEntryPointsWGSL((const char*)sources.sources[i].data);
        for(uint32_t i = 0;i < entryPoints.size();i++){
            rassert(entryPoints[i].second.size() < 15, "Entrypoint name must be shorter than 15 characters");
            char* dest = ret.reflectionInfo.ep[entryPoints[i].first].name;
            char* end = std::copy(entryPoints[i].second.begin(), entryPoints[i].second.end(), dest);
            *end = '\0';
        }
    }
    #else
    ShaderSources spirvSources = wgsl_to_spirv(sources);
    ret = LoadShaderModuleSPIRV(spirvSources);
    #endif
    ret.reflectionInfo.uniforms = callocnewpp(StringToUniformMap);
    ret.reflectionInfo.attributes = CLITERAL(InOutAttributeInfo){0};
    ret.reflectionInfo.uniforms->uniforms = getBindings(sources);
    ret.reflectionInfo.attributes = getAttributesWGSL(sources);
    return ret;
}
static format_or_sample_type extractFormat(const tint::ast::Identifier* iden){
    if(auto templiden = iden->As<tint::ast::TemplatedIdentifier>()){
        for(size_t i = 0;i < templiden->arguments.Length();i++){
            if(templiden->arguments[i]->As<tint::ast::IdentifierExpression>() && templiden->arguments[i]->As<tint::ast::IdentifierExpression>()->identifier){
                auto arg_iden = templiden->arguments[i]->As<tint::ast::IdentifierExpression>()->identifier;
                if(arg_iden->symbol.Name() == "r32float"){
                    return format_r32float;
                }
                else if(arg_iden->symbol.Name() == "r32uint"){
                    return format_r32uint;
                }
                else if(arg_iden->symbol.Name() == "rgba8unorm"){
                    return format_rgba8unorm;
                }
                else if(arg_iden->symbol.Name() == "rgba32float"){
                    return format_rgba32float;
                }
                if(arg_iden->symbol.Name() == "f32"){
                    return sample_f32;
                }
                else if(arg_iden->symbol.Name() == "u32"){
                    return sample_u32;
                }
                //TODO: support all, there's not that many
            }
        }
    }
    TRACELOG(LOG_FATAL, "Shader parse failed");
    return format_or_sample_type(12123);
}

std::vector<uint32_t> wgslToSpirv(const char* wgslSource){
    #ifndef __EMSCRIPTEN__
    tint::Source::File file("", wgslSource);
    tint::Program program = tint::wgsl::reader::Parse(&file);
    auto rmodule = tint::wgsl::reader::ProgramToLoweredIR(program);
    tint::spirv::writer::Options options{};
    auto spirvResult = tint::spirv::writer::Generate(rmodule.Get(), options);
    return spirvResult.Get().spirv;
    #else
    return {};
    #endif
}
access_type extractAccess(const tint::ast::Identifier* iden){
    if(auto templiden = iden->As<tint::ast::TemplatedIdentifier>()){
        for(size_t i = 0;i < templiden->arguments.Length();i++){
            if(templiden->arguments[i]->As<tint::ast::IdentifierExpression>() && templiden->arguments[i]->As<tint::ast::IdentifierExpression>()->identifier){
                auto arg_iden = templiden->arguments[i]->As<tint::ast::IdentifierExpression>()->identifier;
                if(arg_iden->symbol.Name().starts_with("read_write")){
                    return readwrite;
                }
                else if(arg_iden->symbol.Name().starts_with("write")){
                    return writeonly;
                }
                else if(arg_iden->symbol.Name().starts_with("read")){
                    return readonly;
                }
            }
        }
    }
    TRACELOG(LOG_FATAL, "Shader parse failed");
    return access_type(12123);
}
std::vector<std::pair<WGPUShaderStageEnum, std::string>> getEntryPointsWGSL(const char* shaderSourceWGSL){
    std::vector<std::pair<WGPUShaderStageEnum, std::string>> entryPoints;
    tint::Source::File file("", shaderSourceWGSL);
    tint::Program prog = tint::wgsl::reader::Parse(&file);
    tint::inspector::Inspector inspector(prog);
    std::vector<tint::inspector::EntryPoint> eps = inspector.GetEntryPoints();

    for(auto& ep : eps){
        WGPUShaderStageEnum stage;
        switch(ep.stage){
            case tint::inspector::PipelineStage::kVertex:
            stage = WGPUShaderStageEnum_Vertex;
            break;
            case tint::inspector::PipelineStage::kFragment:
            stage = WGPUShaderStageEnum_Fragment;
            break;
            case tint::inspector::PipelineStage::kCompute:
            stage = WGPUShaderStageEnum_Compute;
            break;
            default: rg_unreachable();
        }
        entryPoints.emplace_back(stage, ep.name);
    }
    return entryPoints;
}
#endif
std::pair<std::vector<uint32_t>, WGPUShaderStage> wgsl_to_spirv_single(const char* source){
    std::pair<std::vector<uint32_t>, WGPUShaderStage> ret;
    #if SUPPORT_WGSL_PARSER == 1 && !defined(__EMSCRIPTEN__)
    tint::Source::File sourceFile("", source);
    tint::Program program = tint::wgsl::reader::Parse(&sourceFile);
    auto module = tint::wgsl::reader::ProgramToLoweredIR(program);
    tint::spirv::writer::Options options{};


    tint::Result<tint::spirv::writer::Output> spirvOutput = tint::spirv::writer::Generate(module.Get(), options);
    if(spirvOutput == tint::Success){

    }
    else{
        std::cerr << spirvOutput.Failure().reason;
        std::cerr << "oooooof\n";
    }

    ret.first = spirvOutput.Get().spirv;
    
    return ret;
    #else
    TRACELOG(LOG_FATAL, "WGSL Parser not enabled");
    return ret;
    #endif
}
ShaderSources wgsl_to_spirv(ShaderSources sources){
    ShaderSources ret zeroinit;
    #if SUPPORT_WGSL_PARSER == 1
    rassert(sources.language == sourceTypeWGSL, "Must be WGSL here");
    ret.sourceCount = sources.sourceCount;
    ret.language = sourceTypeSPIRV;

    for(uint32_t i = 0;i < sources.sourceCount;i++){
        WGPUShaderStageEnum stage = (WGPUShaderStageEnum)std::countr_zero((uint32_t)sources.sources[i].stageMask);
        std::vector<uint32_t> stageToSpirv = wgsl_to_spirv_single((const char*)sources.sources[i].data).first;
        uint32_t* odata = (uint32_t*)std::calloc(stageToSpirv.size(), sizeof(uint32_t));
        std::copy(stageToSpirv.begin(), stageToSpirv.end(), odata);
        ret.sources[i].data = odata;
        ret.sources[i].sizeInBytes = stageToSpirv.size() * sizeof(uint32_t);
        ret.sources[i].stageMask = sources.sources[i].stageMask;
    }
    return ret;
    #else
    TRACELOG(LOG_FATAL, "WGSL Parser not enabled");
    return ret;
    #endif
}
InOutAttributeInfo getAttributesWGSL(ShaderSources sources){
    //TODo
    const char* shaderSourceWGSL = (const char*)sources.sources[0].data;
    rassert(shaderSourceWGSL != nullptr, "vertexAndFragmentSource must be set for WGSL");
    
    std::string_view source_view = std::string_view((const char*)sources.sources[0].data, (const char*)sources.sources[0].data + sources.sources[0].sizeInBytes);
    InOutAttributeInfo retvalue;
    
    //TODO attachmentss
#if SUPPORT_WGSL_PARSER == 1
    tint::Source::File f("path", shaderSourceWGSL);
    tint::wgsl::reader::Options options{};
    tint::wgsl::AllowedFeatures features;
    features.features.emplace(tint::wgsl::LanguageFeature::kReadonlyAndReadwriteStorageTextures);
    options.allowed_features = features;
    
    tint::Program result = tint::wgsl::reader::Parse(&f, options);
    if(!result.IsValid()){
        TRACELOG(LOG_ERROR, "Could not parse shader:");
        std::cout << result.Diagnostics() << "\n";
        return {};
    }
    tint::inspector::Inspector insp(result);
    namespace ti = tint::inspector;
    const tint::sem::Info& psem = result.Sem();
    const std::vector<tint::inspector::EntryPoint> entryPoints = insp.GetEntryPoints();
    
    for(size_t i = 0;i < entryPoints.size();i++){
        if(entryPoints[i].stage == ti::PipelineStage::kVertex){
            retvalue.vertexAttributeCount = entryPoints[i].input_variables.size(); 
            for(size_t j = 0;j < entryPoints[i].input_variables.size();j++){
                std::string varname = entryPoints[i].input_variables[j].variable_name;
                if(!entryPoints[i].input_variables[j].attributes.location.has_value())
                    continue;
                uint32_t location = entryPoints[i].input_variables[j].attributes.location.value();
                
                ReflectionVertexAttribute* insert = retvalue.vertexAttributes + j;
                insert->location = location;

                if(varname.size() > MAX_VERTEX_ATTRIBUTE_NAME_LENGTH){
                    TRACELOG(LOG_FATAL, "Vertex attribute exceeds limit: %s", varname.c_str());
                }
                memcpy(insert->name, varname.c_str(), varname.size() + 1);
                
                if(entryPoints[i].input_variables[j].component_type == ti::ComponentType::kF32){
                    switch(entryPoints[i].input_variables[j].composition_type){
                        case ti::CompositionType::kScalar:{
                            insert->format = WGPUVertexFormat_Float32;
                        }break;
                        case ti::CompositionType::kVec2:{
                            insert->format = WGPUVertexFormat_Float32x2;
                        }break;
                        case ti::CompositionType::kVec3:{
                            insert->format = WGPUVertexFormat_Float32x3;
                        }break;
                        case ti::CompositionType::kVec4:{
                            insert->format = WGPUVertexFormat_Float32x4;
                        }break;
                        case ti::CompositionType::kUnknown:{
                            TRACELOG(LOG_ERROR, "Unknown composition type");
                        }break;
                    }
                }else if(entryPoints[i].input_variables[j].component_type == ti::ComponentType::kU32){
                    switch(entryPoints[i].input_variables[j].composition_type){
                        case ti::CompositionType::kScalar:{
                            insert->format = WGPUVertexFormat_Uint32;
                        }break;
                        case ti::CompositionType::kVec2:{
                            insert->format = WGPUVertexFormat_Uint32x2;
                        }break;
                        case ti::CompositionType::kVec3:{
                            insert->format = WGPUVertexFormat_Uint32x3;
                        }break;
                        case ti::CompositionType::kVec4:{
                            insert->format = WGPUVertexFormat_Uint32x4;
                        }break;
                        case ti::CompositionType::kUnknown:{
                            TRACELOG(LOG_ERROR, "Unknown composition type");
                        }break;
                    }
                }
                else if(entryPoints[i].input_variables[j].component_type == ti::ComponentType::kI32){
                    switch(entryPoints[i].input_variables[j].composition_type){
                        case ti::CompositionType::kScalar:{
                            insert->format = WGPUVertexFormat_Sint32;
                        }break;
                        case ti::CompositionType::kVec2:{
                            insert->format = WGPUVertexFormat_Sint32x2;
                        }break;
                        case ti::CompositionType::kVec3:{
                            insert->format = WGPUVertexFormat_Sint32x3;
                        }break;
                        case ti::CompositionType::kVec4:{
                            insert->format = WGPUVertexFormat_Sint32x4;
                        }break;
                        case ti::CompositionType::kUnknown:{
                            TRACELOG(LOG_ERROR, "Unknown composition type");
                        }break;
                    }
                }else if(entryPoints[i].input_variables[j].component_type == ti::ComponentType::kF16){
                    switch(entryPoints[i].input_variables[j].composition_type){
                        case ti::CompositionType::kScalar:{
                            insert->format = WGPUVertexFormat_Float16;
                        }break;
                        case ti::CompositionType::kVec2:{
                            insert->format = WGPUVertexFormat_Float16x2;
                        }break;
                        case ti::CompositionType::kVec3:{
                            TRACELOG(LOG_ERROR, "That should not be possible: vec3<f16>");
                        }break;
                        case ti::CompositionType::kVec4:{
                            insert->format = WGPUVertexFormat_Float16x4;
                        }break;
                        case ti::CompositionType::kUnknown:{
                            TRACELOG(LOG_ERROR, "Unknown composition type");
                        }break;
                    }
                }
                else{
                    TRACELOG(LOG_ERROR, "Unknown component type");
                }
                
                //std::cout << (int)entryPoints[i].input_variables[j].composition_type << ", ";
            }
            //std::cout << "\n";
        }
        else if(entryPoints[i].stage == ti::PipelineStage::kFragment){
            const std::vector<ti::StageVariable> vec_outvariables = entryPoints[i].output_variables;
            retvalue.attachmentCount = vec_outvariables.size();
            for(uint32_t i = 0; i < vec_outvariables.size();i++){
                uint32_t dim = ~0;
                format_or_sample_type type = format_or_sample_type::we_dont_know;
                switch(vec_outvariables[i].composition_type){
                    case ti::CompositionType::kVec4:   dim = 4; break;
                    case ti::CompositionType::kVec3:   dim = 3; break;
                    case ti::CompositionType::kVec2:   dim = 2; break;
                    case ti::CompositionType::kScalar: dim = 1; break;
                    default: rg_trap();
                }
                switch(vec_outvariables[i].component_type){
                    case ti::ComponentType::kF32: type = format_or_sample_type::sample_f32; break;
                    case ti::ComponentType::kU32: type = format_or_sample_type::sample_u32; break;
                    default: rg_trap();
                }
                retvalue.attachments[i] = CLITERAL(ReflectionFragmentOutput){
                    .number_of_components = dim,
                    .type = sample_f32,
                    // TODO: map location
                    // outvar.attributes.location.value_or(LOCATION_NOT_FOUND),
                    // type
                };
            }
        }
    }
#endif
    return retvalue;
}
std::unordered_map<std::string, ResourceTypeDescriptor> getBindingsWGSL(ShaderSources sources){
    std::unordered_map<std::string, ResourceTypeDescriptor> ret;
    
#if SUPPORT_WGSL_PARSER == 1
    tint::Source::File f("path", (const char*)sources.sources[0].data);
    tint::wgsl::reader::Options options{};
    tint::wgsl::AllowedFeatures features;
    features.features.emplace(tint::wgsl::LanguageFeature::kReadonlyAndReadwriteStorageTextures);
    options.allowed_features = features;

    tint::Program result = tint::wgsl::reader::Parse(&f, options);
    //tint::Result<tint::core::ir::Module> modres = tint::wgsl::reader::ProgramToLoweredIR(result);
    //tint::core::ir::Module mod = std::move(modres.Get());
    //auto spirv_raise_result = tint::spirv::writer::Generate(mod, tint::spirv::writer::Options{});

    tint::inspector::Inspector insp(result);
    if(!result.IsValid()){
        TRACELOG(LOG_ERROR, "Could not parse shader:");
        std::cout << result.Diagnostics() << "\n";
        return {};
    }
    const tint::sem::Info& psem = result.Sem();
    for(size_t i = 0;i < result.AST().GlobalVariables().Length();i++){
        auto ast_equivalent = result.AST().GlobalVariables()[i];
        if(ast_equivalent->As<tint::ast::Const>() || ast_equivalent->As<tint::ast::Override>()){
            continue; // Skip constants, only process uniforms
        }
        if(auto var = ast_equivalent->As<tint::ast::Var>()){
            if(var->declared_address_space && var->declared_address_space->As<tint::ast::IdentifierExpression>()){
                if(var->declared_address_space->As<tint::ast::IdentifierExpression>()->identifier){
                    if(var->declared_address_space->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name() == "private"){
                        continue; //continue var<private>
                    }
                }
            }
        }
        auto sgvar = psem.Get(result.AST().GlobalVariables()[i])->As<tint::sem::GlobalVariable>();
        ResourceTypeDescriptor desc{};
        std::stringstream sstr;
        std::string varname = sgvar->Declaration()->name->symbol.Name();
        sstr << sgvar->Type()->FriendlyName();
        TRACELOG(LOG_DEBUG, "Parsing uniform %s of type %s", varname.c_str(), sstr.str().c_str());
        desc.minBindingSize = sgvar->Type()->As<tint::core::type::Reference>()->UnwrapRef()->Size();
        desc.location = sgvar->Attributes().binding_point->binding;
        auto iden = result.AST().GlobalVariables()[i]->type->As<tint::ast::IdentifierExpression>()->identifier;
        auto& glob = result.AST().GlobalVariables()[i];
        if(iden->symbol.Name().starts_with("texture_2d")){
            if(iden->symbol.Name().starts_with("texture_2d_array")){
                desc.type = texture2d_array;
            }
            else{
                desc.type = texture2d;
            }
            desc.fstype = extractFormat(iden);
        }
        if(iden->symbol.Name().starts_with("texture_3d")){
            desc.type = texture3d;
            desc.fstype = extractFormat(iden);
        }
        if(iden->symbol.Name().starts_with("texture_storage_2d")){
            if(iden->symbol.Name().starts_with("texture_storage_2d_array")){
                desc.type = storage_texture2d_array;
            }
            else{
                desc.type = storage_texture2d;
            }
            desc.access = extractAccess(iden);
            desc.fstype = extractFormat(iden);
        }
        if(iden->symbol.Name().starts_with("texture_storage_3d")){
            desc.type = storage_texture3d;
            desc.access = extractAccess(iden);
            desc.fstype = extractFormat(iden);
        }
        else if(iden->symbol.Name().starts_with("sampler")){
            desc.type = texture_sampler;
        }
        else{
            if(glob->As<tint::ast::Var>()->declared_address_space){
                if(auto addrspace = glob->As<tint::ast::Var>()->declared_address_space->As<tint::ast::IdentifierExpression>()){
                    if(addrspace->identifier->symbol.Name() == "uniform"){
                        desc.type = uniform_buffer;
                    }
                    else if(addrspace->identifier->symbol.Name() == "storage"){
                        desc.type = storage_buffer;
                        if(glob->As<tint::ast::Var>()->declared_access){
                            if(auto accex = glob->As<tint::ast::Var>()->declared_access->As<tint::ast::IdentifierExpression>()){
                                std::string access_modifier = accex->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name();
                                if(access_modifier == "read"){
                                    desc.access = readonly;
                                }
                                else if (access_modifier == "read_write"){
                                    desc.access = readwrite;
                                }
                            }
                        }
                        else{
                            desc.type = storage_buffer;
                        }
                    }
                }
            }
        }       
        
        ret[sgvar->Declaration()->name->symbol.Name()] = desc;
        //std::cout << sgvar->Attributes().binding_point.value() << " ";
        //std::cout << sgvar->Type()->As<tint::core::type::Reference>()->UnwrapRef()->Size() << "\n";
    }
    //auto mod = tint::wgsl::reader::ProgramToLoweredIR(result);
    //std::cout << mod << std::endl;
    //auto glslresult = tint::glsl::writer::Generate(mod.Get(), tint::glsl::writer::Options{}, std::string("vs_main"));
    //std::cout << glslresult << std::endl;
    //std::cout << glslresult.Get().glsl << "\n";
#endif
    return ret;
    /*std::unordered_map<std::string, std::unordered_map<uint32_t, VertexFormat>> declaredTypesAndLocations;
    if(result.IsValid()){

        for(auto& str : result.AST().TypeDecls()){
            std::string structname =  str->name->symbol.Name();
            //std::cout << structname << "\n";
            if(auto structDecl = str->As<tint::ast::Struct>()){
                for(auto& mem : structDecl->members){
                    //std::cout << mem->type->TypeInfo().name << "\n";
                    //std::cout << "  " << mem->type->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name() << "\n";
                    
                    //bool hasLocationAttribute = false;
                    for(auto& att : mem->attributes){
                        if(auto loca = att->As<tint::ast::LocationAttribute>()){
                            //hasLocationAttribute = true;
                            if(auto intliteral = loca->expr->As<tint::ast::IntLiteralExpression>()){
                                uint32_t v = intliteral->value;
                                if(intliteral->value < 0)
                                    TRACELOG(LOG_WARNING, "Negative attribute location detected");
                                if(auto templ = mem->type->As<tint::ast::IdentifierExpression>()->identifier->As<tint::ast::TemplatedIdentifier>()){
                                    std::string arg0 = templ->arguments[0]->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name();
                                      declaredTypesAndLocations[structname][v] = builtins_templated.at(mem->type->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name()).at(arg0);
                                }
                                else{
                                    declaredTypesAndLocations[structname][v] = builtins.at(mem->type->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name());
                                }
                            }
                        }
                    }
                }
            }
        }
        for(auto& glob : result.AST().GlobalVariables()){
            UniformDescriptor desc{};
            desc.minBindingSize = 4;
            auto iden = glob->type->As<tint::ast::IdentifierExpression>()->identifier;
            if(auto templ_iden = iden->As<tint::ast::TemplatedIdentifier>()){
                for(auto arg : templ_iden->arguments){
                    //std::cout << arg->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name() << "\n";
                }
            }
            uint32_t bindgroup = 0;
            uint32_t bindpoint = 0;
            for(uint32_t i = 0;i < glob->attributes.Length();i++){
                if(auto gattrib = glob->attributes[i]->As<tint::ast::GroupAttribute>()){
                    bindgroup = gattrib->expr->As<tint::ast::IntLiteralExpression>()->value;
                }
                else if(auto battrib = glob->attributes[i]->As<tint::ast::BindingAttribute>()){
                    bindpoint = battrib->expr->As<tint::ast::IntLiteralExpression>()->value;
                }
            }
            if(bindgroup != 0){
                TRACELOG(LOG_WARNING, "Detected @group attribute != 0: %u", (unsigned)bindgroup);
            }
            

            if(iden->symbol.Name().starts_with("texture_2d")){
                desc.type = texture2d;
            }
            else if(iden->symbol.Name().starts_with("sampler")){
                desc.type = sampler;
            }
            else{
                
                
                if(auto addrspace = glob->As<tint::ast::Var>()->declared_address_space->As<tint::ast::IdentifierExpression>()){
                    if(addrspace->identifier->symbol.Name() == "uniform"){
                        desc.type = uniform_buffer;
                    }
                    else if(addrspace->identifier->symbol.Name() == "storage"){
                        
                        if(auto accex = glob->As<tint::ast::Var>()->declared_access->As<tint::ast::IdentifierExpression>()){
                            std::string access_modifier = accex->As<tint::ast::IdentifierExpression>()->identifier->symbol.Name();
                            if(access_modifier == "read"){
                                desc.type = storage_buffer;
                            }
                            else if (access_modifier == "read_write"){
                                desc.type = storage_write_buffer;
                            }
                        }
                        else{
                            desc.type = storage_buffer;
                        }
                    }
                }
                
                
                //std::cout << ">" << std::endl;
                //for(auto attr : glob->TypeInfo().name){
                //    std::cout <<  << "\n";
                //}
                //desc.
            }
            desc.location = bindpoint;
            ret[glob->name->symbol.Name()] = desc;
            //std::cout <<  << "\n";
            //std::cout << glob->type->As<tint::ast::IdentifierExpression>()->identifier->TypeInfo().name << "\n";
            //std::cout << glob->name->symbol.Name() << " at " << glob->attributes[0]->As<tint::ast::GroupAttribute>()->expr->As<tint::ast::IntLiteralExpression>()->value << " : " << glob->attributes[1]->As<tint::ast::BindingAttribute>()->expr->As<tint::ast::IntLiteralExpression>()->value << "\n";
        }
        
        for(auto& glob : result.AST().Functions()){
            //std::cout << "fn " << glob->params[0]->name->TypeInfo().base->name << "\n";
            //std::cout << "fn " << glob->name->symbol.Name() << "\n";
        }
        
        
    }
    else{
        std::cout << result.Diagnostics() << std::endl;
    }
    //tint::Source source();
    return ret;*/
}
/*std::unordered_map<std::string, UniformDescriptor> getBindings(const char* shaderSource){
    std::istringstream istr(shaderSource);
    std::string accum{};
    std::string line;
    while(std::getline(istr, line)){
        size_t n = line.find("//");
        if(n != std::string::npos){
            line.resize(n);
        }
        accum += line;
    }
    std::vector<UniformDescriptor> ret;
    for (auto match : ctre::search_all<"@group\\(([0-9]+)\\) +@binding\\(([0-9]+)\\) *var(< *storage[^>]*>|< *uniform *>){0,1} *([a-zA-Z0-9_]+) *: *([^;]+)">(accum)) {
        UniformDescriptor val;
        val.minBindingSize = 4;
        std::string_view storagetype{match.get<3>()};
        std::string_view type{match.get<5>()};
        if(!storagetype.empty()){
            std::cout << storagetype << "\n";
        }
        if(storagetype.empty()){
            if(type.starts_with("texture_2d")){
                val.type = texture2d;
            }
            else{
                val.type = sampler;
            }
        }
        else{
            if(auto matsch = ctre::search<"< *([a-z]+) *(, *([a-z_]+)){0,1} *>">(storagetype)){
                if(!matsch.get<3>()){
                    val.type = storage_buffer;
                }
                else if(matsch.get<3>() == "read_write"){
                    val.type = storage_write_buffer;
                }
                else if(matsch.get<3>() == "read"){
                    val.type = storage_buffer;
                }
                else{
                    std::string prv(match.get<0>());
                    TRACELOG(LOG_WARNING, "Could not parse entry %s", prv.c_str());
                }
            }
            else{
                std::string prv(match.get<0>());
                TRACELOG(LOG_WARNING, "Could not parse entry %s", prv.c_str());
            }
            
        }
        ret.push_back(val);
    }
    return ret;
}
std::unordered_map<std::string, std::pair<VertexFormat, uint32_t>> getAttributes(const char* shaderSource){
    std::unordered_map<std::string, std::pair<VertexFormat, uint32_t>> retval;
    std::istringstream istr(shaderSource);
    std::string accum{};
    std::string line;
    while(std::getline(istr, line)){
        size_t n = line.find("//");
        if(n != std::string::npos){
            line.resize(n);
        }
        accum += line;
    }
    //std::cout << accum << "\n";
    if(auto match = ctre::search<"struct +VertexInput *\\{.*?\\}">(accum)) {
        std::string matsch(match);
        for(auto attr : ctre::search_all<"@location\\(([0-9]+)\\) *([a-zA-Z_]+) *: *([^,]+),">(matsch)){
            //std::cout << attr.get<1>() << ',';
            //std::cout << attr.get<2>() << ',';
            //std::cout << attr.get<3>() << '\n';
        }
    }
    else{
        TRACELOG(LOG_WARNING, "Shader code does not contain struct VertexInput, can't parse it");
    }
    return retval;
}*/

std::vector<uint32_t> wgsl_to_spirv(const char* wgslCode){
    //Construct file without filename
    #if SUPPORT_WGSL_PARSER == 1 && !defined(__EMSCRIPTEN__)
    tint::Source::File f("", wgslCode);

    tint::Program prog = tint::wgsl::reader::Parse(&f);
    auto maybeModule = tint::wgsl::reader::ProgramToLoweredIR(prog);
    tint::core::ir::Module module(std::move(maybeModule.Get()));
    
    tint::spirv::writer::Options options zeroinit;
    auto spirvMaybe = tint::spirv::writer::Generate(module, options);

    return spirvMaybe.Get().spirv;
    #else
    TRACELOG(LOG_FATAL, "WGSL Parser not enabled");
    return {};
    #endif
}
#define fprefix wgvk
#define CAT_I(a, b) a ## b
#define CAT(a, b) CAT_I(a, b)

void CAT(fprefix, create)() {
    // function body
}

InOutAttributeInfo getAttributes(ShaderSources sources){
    
    rassert(sources.language != ShaderSourceType::sourceTypeUnknown, "Source type must be known");
    const ShaderSourceType language = sources.language;
    if(language == sourceTypeGLSL){
        #if SUPPORT_GLSL_PARSER == 1
        return getAttributesGLSL(sources);
        #endif
        TRACELOG(LOG_FATAL, "Attempted to get GLSL attributes without GLSL parser enabled");
    }
    if(language == sourceTypeWGSL){
        #if SUPPORT_WGSL_PARSER == 1
        return getAttributesWGSL(sources);
        #endif
        TRACELOG(LOG_FATAL, "Attempted to get WGSL attributes without WGSL parser enabled");
    }
    if(language == sourceTypeSPIRV){
        TRACELOG(LOG_FATAL, "Attempted to get SPIRV attributes, not yet implemented");
    }
    return {};
}
std::unordered_map<std::string, ResourceTypeDescriptor> getBindings(ShaderSources sources){
    
    rassert(sources.language != ShaderSourceType::sourceTypeUnknown, "Source type must be known");
    const ShaderSourceType language = sources.language;
    if(language == sourceTypeGLSL){
        #if SUPPORT_GLSL_PARSER == 1
        return getBindingsGLSL(sources);
        #endif
        TRACELOG(LOG_FATAL, "Attempted to get GLSL bindings without GLSL parser enabled");
    }
    if(language == sourceTypeWGSL){
        #if SUPPORT_WGSL_PARSER == 1
        return getBindingsWGSL(sources);
        #endif
        TRACELOG(LOG_FATAL, "Attempted to get WGSL bindings without WGSL parser enabled");
    }
    if(language == sourceTypeSPIRV){
        TRACELOG(LOG_FATAL, "Attempted to get SPIRV bindings, not yet implemented");
    }
    return {};
}