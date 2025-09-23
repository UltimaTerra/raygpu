// begin file src/shader_parse.cpp
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

#include <config.h>
#include <raygpu.h>
#include <internals.hpp>



RGAPI InOutAttributeInfo                                      getAttributesWGSL_Simple(ShaderSources sources);
RGAPI StringToUniformMap*                                     getBindingsWGSL_Simple  (ShaderSources sources);
RGAPI EntryPointSet                                           getEntryPointsWGSL_Simple(const char* shaderSourceWGSL);

RGAPI InOutAttributeInfo                                      getAttributesWGSL_Tint  (ShaderSources sources);
RGAPI StringToUniformMap*                                     getBindingsWGSL_Tint    (ShaderSources sources);
RGAPI EntryPointSet                                           getEntryPointsWGSL_Tint (const char* shaderSourceWGSL);

RGAPI InOutAttributeInfo                                      getAttributesWGSL       (ShaderSources sources);
RGAPI StringToUniformMap*                                     getBindingsWGSL         (ShaderSources sources);
RGAPI EntryPointSet                                           getEntryPointsWGSL      (const char* shaderSourceWGSL);



#if defined(SUPPORT_TINT_WGSL_PARSER) && (SUPPORT_TINT_WGSL_PARSER == 1)

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tint/tint.h>
#include "src/tint/lang/wgsl/reader/reader.h"
#include "src/tint/lang/wgsl/reader/parser/parser.h"
#include "src/tint/lang/wgsl/program/program_builder.h"
#include "src/tint/lang/wgsl/ast/identifier_expression.h"
#include "src/tint/lang/wgsl/ast/module.h"
#include "src/tint/lang/wgsl/ast/templated_identifier.h"
#include "src/tint/lang/core/type/reference.h"
#include "src/tint/lang/wgsl/ast/var.h"
#include "src/tint/lang/wgsl/ast/override.h"
#include "src/tint/lang/wgsl/ast/const.h"
#include "src/tint/lang/wgsl/inspector/inspector.h"

static inline WGPUVertexFormat ti_format_from(tint::inspector::ComponentType ct,
                                              tint::inspector::CompositionType comp) {
    using CT = tint::inspector::ComponentType;
    using CP = tint::inspector::CompositionType;
    switch (ct) {
        case CT::kF32:
            switch (comp) {
                case CP::kScalar: return WGPUVertexFormat_Float32;
                case CP::kVec2:   return WGPUVertexFormat_Float32x2;
                case CP::kVec3:   return WGPUVertexFormat_Float32x3;
                case CP::kVec4:   return WGPUVertexFormat_Float32x4;
                default:          break;
            }
            break;
        case CT::kU32:
            switch (comp) {
                case CP::kScalar: return WGPUVertexFormat_Uint32;
                case CP::kVec2:   return WGPUVertexFormat_Uint32x2;
                case CP::kVec3:   return WGPUVertexFormat_Uint32x3;
                case CP::kVec4:   return WGPUVertexFormat_Uint32x4;
                default:          break;
            }
            break;
        case CT::kI32:
            switch (comp) {
                case CP::kScalar: return WGPUVertexFormat_Sint32;
                case CP::kVec2:   return WGPUVertexFormat_Sint32x2;
                case CP::kVec3:   return WGPUVertexFormat_Sint32x3;
                case CP::kVec4:   return WGPUVertexFormat_Sint32x4;
                default:          break;
            }
            break;
        case CT::kF16:
            switch (comp) {
                case CP::kScalar: return WGPUVertexFormat_Float16;
                case CP::kVec2:   return WGPUVertexFormat_Float16x2;
                case CP::kVec3:   /* not valid */ break;
                case CP::kVec4:   return WGPUVertexFormat_Float16x4;
                default:          break;
            }
            break;
        default: break;
    }
#ifdef WGPUVertexFormat_Undefined
    return WGPUVertexFormat_Undefined;
#else
    return WGPUVertexFormat_Float32;
#endif
}

static inline WGPUShaderStageEnum ti_stage_to_enum(tint::inspector::PipelineStage st) {
    using PS = tint::inspector::PipelineStage;
    switch (st) {
        case PS::kVertex:   return WGPUShaderStageEnum_Vertex;
        case PS::kFragment: return WGPUShaderStageEnum_Fragment;
        case PS::kCompute:  return WGPUShaderStageEnum_Compute;
        default:            return WGPUShaderStageEnum_Vertex;
    }
}

static inline format_or_sample_type ti_parse_format(const std::string& s) {
    if (s == "r32float")    return format_r32float;
    if (s == "r32uint")     return format_r32uint;
    if (s == "rgba8unorm")  return format_rgba8unorm;
    if (s == "rgba32float") return format_rgba32float;
    if (s == "f32")         return sample_f32;
    if (s == "u32")         return sample_u32;
    return we_dont_know;
}

static inline access_type ti_parse_access(const std::string& s) {
    if (s == "read")        return readonly;
    if (s == "write")       return writeonly;
    if (s == "read_write")  return readwrite;
    return readwrite;
}

getEntryPointsWGSL
getEntryPointsWGSL_Tint(const char* shaderSourceWGSL) {
    std::vector<std::pair<WGPUShaderStageEnum,std::string>> out;
    if (!shaderSourceWGSL) return out;

    tint::Source::File file("", shaderSourceWGSL);
    tint::Program prog = tint::wgsl::reader::Parse(&file);
    if (!prog.IsValid()) return out;

    tint::inspector::Inspector insp(prog);
    auto eps = insp.GetEntryPoints();
    for (auto& ep : eps) {
        out.emplace_back(ti_stage_to_enum(ep.stage), ep.name);
    }
    return out;
}

InOutAttributeInfo getAttributesWGSL_Tint(ShaderSources sources) {
    InOutAttributeInfo info{};
    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr) return info;

    const char* src = (const char*)sources.sources[0].data;
    tint::Source::File file("", src);
    tint::Program prog = tint::wgsl::reader::Parse(&file);
    if (!prog.IsValid()) return info;

    tint::inspector::Inspector insp(prog);
    auto eps = insp.GetEntryPoints();

    for (auto& ep : eps) {
        if (ep.stage != tint::inspector::PipelineStage::kVertex) continue;

        const auto& inputs = ep.input_variables;
        info.vertexAttributeCount = (uint32_t)inputs.size();
        for (size_t i = 0; i < inputs.size() && i < MAX_VERTEX_ATTRIBUTES; ++i) {
            const auto& v = inputs[i];
            ReflectionVertexAttribute* out = &info.vertexAttributes[i];

            if (v.attributes.location.has_value()) out->location = v.attributes.location.value();
            else out->location = LOCATION_NOT_FOUND;

            out->format = ti_format_from(v.component_type, v.composition_type);
            out->name[0] = '\0';
        }
        break; // first vertex EP only
    }

    // Fragment outputs: not strictly required by many pipelines; leave zeroed.
    return info;
}

std::unordered_map<std::string, ResourceTypeDescriptor>
getBindingsWGSL_Tint(ShaderSources sources) {
    std::unordered_map<std::string, ResourceTypeDescriptor> out;
    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr) return out;

    const char* src = (const char*)sources.sources[0].data;
    tint::Source::File file("", src);
    tint::wgsl::reader::Options options{};
    tint::Program prog = tint::wgsl::reader::Parse(&file, options);
    if (!prog.IsValid()) return out;

    // Walk AST globals to classify bindings.
    for (auto* g : prog.AST().GlobalVariables()) {
        auto* var = g->As<tint::ast::Var>();
        if (!var) continue;

        // Skip non-bindable address spaces quickly
        if (!var->declared_address_space) continue;

        ResourceTypeDescriptor desc{};
        std::string name = var->name->symbol.Name();

        // Binding attribute
        for (auto* attr : var->attributes) {
            if (auto* ba = attr->As<tint::ast::BindingAttribute>()) {
                if (auto* il = ba->expr->As<tint::ast::IntLiteralExpression>())
                    desc.location = (uint32_t)il->value;
            }
        }

        // Type classification
        if (auto* ty_expr = var->type->As<tint::ast::IdentifierExpression>()) {
            auto* id = ty_expr->identifier;
            const std::string tname = id->symbol.Name();

            // Textures and samplers
            if (tname.rfind("texture_storage_2d", 0) == 0 ||
                tname.rfind("texture_storage_3d", 0) == 0) {
                bool is2d = tname.rfind("texture_storage_2d", 0) == 0;
                bool is3d = tname.rfind("texture_storage_3d", 0) == 0;
                bool is_array = tname.find("_2d_array") != std::string::npos;

                desc.type =
                    is3d ? storage_texture3d :
                    (is2d && is_array ? storage_texture2d_array : storage_texture2d);

                if (auto* tid = id->As<tint::ast::TemplatedIdentifier>()) {
                    // <FORMAT, ACCESS>
                    if (tid->arguments.Length() >= 1) {
                        if (auto* a0 = tid->arguments[0]->As<tint::ast::IdentifierExpression>())
                            desc.fstype = ti_parse_format(a0->identifier->symbol.Name());
                    }
                    if (tid->arguments.Length() >= 2) {
                        if (auto* a1 = tid->arguments[1]->As<tint::ast::IdentifierExpression>())
                            desc.access = ti_parse_access(a1->identifier->symbol.Name());
                    }
                }
                out.emplace(name, desc);
                continue;
            }
            if (tname.rfind("texture_2d", 0) == 0 || tname.rfind("texture_3d", 0) == 0) {
                bool is2d = tname.rfind("texture_2d", 0) == 0;
                bool is3d = tname.rfind("texture_3d", 0) == 0;
                bool is_array = tname.find("_2d_array") != std::string::npos;

                desc.type =
                    is3d ? texture3d :
                    (is2d && is_array ? texture2d_array : texture2d);

                if (auto* tid = id->As<tint::ast::TemplatedIdentifier>()) {
                    // <SAMPLE_T> e.g. <f32>
                    if (tid->arguments.Length() >= 1) {
                        if (auto* a0 = tid->arguments[0]->As<tint::ast::IdentifierExpression>())
                            desc.fstype = ti_parse_format(a0->identifier->symbol.Name());
                    }
                }
                out.emplace(name, desc);
                continue;
            }
            if (tname.rfind("sampler", 0) == 0) {
                desc.type = texture_sampler;
                out.emplace(name, desc);
                continue;
            }
        }

        // Buffers via address space
        if (auto* as_expr = var->declared_address_space->As<tint::ast::IdentifierExpression>()) {
            const std::string as = as_expr->identifier->symbol.Name();
            if (as == "uniform") {
                desc.type = uniform_buffer;
                out.emplace(name, desc);
            } else if (as == "storage") {
                desc.type = storage_buffer;
                // Access may be specified on var<storage, read|read_write>
                if (var->declared_access) {
                    if (auto* acc = var->declared_access->As<tint::ast::IdentifierExpression>()) {
                        desc.access = ti_parse_access(acc->identifier->symbol.Name());
                    }
                } else {
                    desc.access = readwrite;
                }
                out.emplace(name, desc);
            }
        }
    }

    return out;
}

#endif // tint backend
#if SUPPORT_VULKAN_BACKEND == 1



#endif



DescribedShaderModule LoadShaderModuleWGSL(ShaderSources sources) {
    
    DescribedShaderModule ret = {0};
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
        
        EntryPointSet entryPoints = getEntryPointsWGSL((const char*)sources.sources[i].data);
        for(uint32_t i = 0;i < 16;i++){
            //rassert(entryPoints[i].second.size() < 15, "Entrypoint name must be shorter than 15 characters");
            if(entryPoints.names[i][0] == '\0'){
                continue;
            }
            char* dest = ret.reflectionInfo.ep[i].name;
            memcpy(dest, entryPoints.names[i], MAX_SHADER_ENTRYPOINT_NAME_LENGTH + 1);
        }
    }
    #else
    ShaderSources spirvSources = wgsl_to_spirv(sources);
    ret = LoadShaderModuleSPIRV(spirvSources);
    #endif
    ret.reflectionInfo.uniforms = callocnewpp(StringToUniformMap);
    ret.reflectionInfo.attributes = CLITERAL(InOutAttributeInfo){0};
    ret.reflectionInfo.uniforms = getBindings(sources);
    ret.reflectionInfo.attributes = getAttributesWGSL(sources);
    return ret;
}




// end file src/shader_parse.cpp