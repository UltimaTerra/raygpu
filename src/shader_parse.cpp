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

#include "config.h"
#include <raygpu.h>
#include <internals.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>


InOutAttributeInfo                                      getAttributesWGSL_Simple(ShaderSources sources);
StringToUniformMap*                                     getBindingsWGSL_Simple  (ShaderSources sources);
EntryPointSet                                           getEntryPointsWGSL_Simple(const char* shaderSourceWGSL);

InOutAttributeInfo                                      getAttributesWGSL_Tint  (ShaderSources sources);
StringToUniformMap*                                     getBindingsWGSL_Tint    (ShaderSources sources);
EntryPointSet                                           getEntryPointsWGSL_Tint (const char* shaderSourceWGSL);

InOutAttributeInfo                                      getAttributesWGSL       (ShaderSources sources);
StringToUniformMap*                                     getBindingsWGSL         (ShaderSources sources);
EntryPointSet                                           getEntryPointsWGSL      (const char* shaderSourceWGSL);

// =================================================================================================
// SIMPLE WGSL PARSER BACKEND
// Enabled when SUPPORT_TINT_WGSL_PARSER != 1
// =================================================================================================
#if !defined(SUPPORT_TINT_WGSL_PARSER) || (SUPPORT_TINT_WGSL_PARSER != 1)

extern "C" {
    // Pull in the C parser/resolver only for the simple backend.
    #include "simple_wgsl/wgsl_parser.c"
    //#include "simple_wgsl/wgsl_parser.h"
    #include "simple_wgsl/wgsl_resolve.c"
    //#include "simple_wgsl/wgsl_resolve.h"
}

static inline bool sw_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    const size_t n = std::strlen(prefix);
    return std::strncmp(s, prefix, n) == 0;
}

static inline WGPUShaderStageEnum sw_to_stage_enum(WgslStage st) {
    switch (st) {
        case WGSL_STAGE_VERTEX:   return WGPUShaderStageEnum_Vertex;
        case WGSL_STAGE_FRAGMENT: return WGPUShaderStageEnum_Fragment;
        case WGSL_STAGE_COMPUTE:  return WGPUShaderStageEnum_Compute;
        default:                  return WGPUShaderStageEnum_Vertex;
    }
}

static inline WGPUVertexFormat sw_vf_from_numeric(int comps, WgslNumericType nt) {
    switch (nt) {
        case WGSL_NUM_F32:
            switch (comps) {
                case 1: return WGPUVertexFormat_Float32;
                case 2: return WGPUVertexFormat_Float32x2;
                case 3: return WGPUVertexFormat_Float32x3;
                case 4: return WGPUVertexFormat_Float32x4;
            } break;
        case WGSL_NUM_I32:
            switch (comps) {
                case 1: return WGPUVertexFormat_Sint32;
                case 2: return WGPUVertexFormat_Sint32x2;
                case 3: return WGPUVertexFormat_Sint32x3;
                case 4: return WGPUVertexFormat_Sint32x4;
            } break;
        case WGSL_NUM_U32:
        case WGSL_NUM_BOOL:
            switch (comps) {
                case 1: return WGPUVertexFormat_Uint32;
                case 2: return WGPUVertexFormat_Uint32x2;
                case 3: return WGPUVertexFormat_Uint32x3;
                case 4: return WGPUVertexFormat_Uint32x4;
            } break;
        case WGSL_NUM_F16:
            switch (comps) {
                case 1: return WGPUVertexFormat_Float16;
                case 2: return WGPUVertexFormat_Float16x2;
                // vec3<f16> is not valid for vertex formats
                case 4: return WGPUVertexFormat_Float16x4;
            } break;
        default: break;
    }
#ifdef WGPUVertexFormat_Undefined
    return WGPUVertexFormat_Undefined;
#else
    return WGPUVertexFormat_Float32;
#endif
}

static inline format_or_sample_type sw_parse_format_token(const char* tkn) {
    if (!tkn) return we_dont_know;
    if (std::strcmp(tkn, "r32float") == 0)    return format_r32float;
    if (std::strcmp(tkn, "r32uint") == 0)     return format_r32uint;
    if (std::strcmp(tkn, "rgba8unorm") == 0)  return format_rgba8unorm;
    if (std::strcmp(tkn, "rgba32float") == 0) return format_rgba32float;
    if (std::strcmp(tkn, "f32") == 0)         return sample_f32;
    if (std::strcmp(tkn, "u32") == 0)         return sample_u32;
    return we_dont_know;
}

static inline access_type sw_parse_access_token(const char* tkn) {
    if (!tkn) return readwrite;
    if (std::strcmp(tkn, "read") == 0)        return readonly;
    if (std::strcmp(tkn, "write") == 0)       return writeonly;
    if (std::strcmp(tkn, "read_write") == 0)  return readwrite;
    return readwrite;
}

struct SW_ParsedTextureMeta {
    bool is_storage = false;
    bool is_array   = false;
    bool is_3d      = false;
    format_or_sample_type fmt_or_sample = we_dont_know;
    access_type access = readwrite;
};

static inline SW_ParsedTextureMeta sw_parse_texture_typenode(const WgslAstNode* T) {
    SW_ParsedTextureMeta m{};
    if (!T || T->type != WGSL_NODE_TYPE) return m;
    const char* name = T->type_node.name ? T->type_node.name : "";

    m.is_storage = sw_starts_with(name, "texture_storage_");
    m.is_array   = sw_starts_with(name, "texture_2d_array") || sw_starts_with(name, "texture_storage_2d_array");
    m.is_3d      = sw_starts_with(name, "texture_3d") || sw_starts_with(name, "texture_storage_3d");

    for (int i = 0; i < T->type_node.type_arg_count; ++i) {
        const WgslAstNode* a = T->type_node.type_args[i];
        if (a && a->type == WGSL_NODE_TYPE && a->type_node.name) {
            format_or_sample_type f = sw_parse_format_token(a->type_node.name);
            if (f != we_dont_know) {
                m.fmt_or_sample = f;
                continue;
            }
            m.access = sw_parse_access_token(a->type_node.name);
        }
    }
    return m;
}

EntryPointSet getEntryPointsWGSL_Simple(const char* shaderSourceWGSL) {
    EntryPointSet eps;
    if (!shaderSourceWGSL) return eps;

    WgslAstNode* ast = wgsl_parse(shaderSourceWGSL);
    if (!ast) return eps;

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) { wgsl_free_ast(ast); return eps; }

    int n = 0;
    const WgslResolverEntrypoint* arr = wgsl_resolver_entrypoints(R, &n);
    uint32_t insertIndex = 0;
    for (int i = 0; i < n; ++i) {
        char* insert = eps.names[sw_to_stage_enum(arr[i].stage)];
        if(arr[i].name && strlen(arr[i].name) <= MAX_SHADER_ENTRYPOINT_NAME_LENGTH){
            memcpy(insert, arr[i].name, strlen(arr[i].name));
        }
    }

    wgsl_resolve_free((void*)arr);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return eps;
}

InOutAttributeInfo getAttributesWGSL_Simple(ShaderSources sources) {
    InOutAttributeInfo info{};
    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr) return info;

    const char* src = (const char*)sources.sources[0].data;

    WgslAstNode* ast = wgsl_parse(src);
    if (!ast) return info;

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) { wgsl_free_ast(ast); return info; }

    int ep_count = 0;
    const WgslResolverEntrypoint* eps = wgsl_resolver_entrypoints(R, &ep_count);

    const char* vertex_name = nullptr;
    for (int i = 0; i < ep_count; ++i) {
        if (eps[i].stage == WGSL_STAGE_VERTEX) { vertex_name = eps[i].name; break; }
    }

    if (vertex_name) {
        WgslVertexSlot* slots = nullptr;
        int slot_count = wgsl_resolver_vertex_inputs(R, vertex_name, &slots);
        info.vertexAttributeCount = (uint32_t)slot_count;
        for (int i = 0; i < slot_count && i < (int)MAX_VERTEX_ATTRIBUTES; ++i) {
            ReflectionVertexAttribute* out = &info.vertexAttributes[i];
            out->location = (uint32_t)slots[i].location;
            out->format   = sw_vf_from_numeric(slots[i].component_count, slots[i].numeric_type);
            out->name[0]  = '\0';
        }
        wgsl_resolve_free(slots);
    }

    wgsl_resolve_free((void*)eps);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return info;
}

StringToUniformMap* getBindingsWGSL_Simple(ShaderSources sources) {
    StringToUniformMap* out = NULL;
    if (sources.sourceCount == 0 || sources.sources[0].data == nullptr){
        return out;
    }
    out = callocnew(StringToUniformMap);
    StringToUniformMap_init(out);
    const char* src = (const char*)sources.sources[0].data;

    WgslAstNode* ast = wgsl_parse(src);
    if (!ast){
        return out;
    }

    WgslResolver* R = wgsl_resolver_build(ast);
    if (!R) {
        wgsl_free_ast(ast);
        return out;
    }

    int n = 0;
    const WgslSymbolInfo* syms = wgsl_resolver_binding_vars(R, &n);
    for (int i = 0; i < n; ++i) {
        const WgslSymbolInfo& s = syms[i];
        if (!s.name || !s.decl_node || s.decl_node->type != WGSL_NODE_GLOBAL_VAR) continue;

        const GlobalVar& gv = s.decl_node->global_var;

        ResourceTypeDescriptor desc{};
        desc.location = (uint32_t)(s.has_binding ? s.binding_index : 0);
        if (s.has_min_binding_size) desc.minBindingSize = (uint32_t)s.min_binding_size;

        const WgslAstNode* T = gv.type;
        const char* tname = (T && T->type == WGSL_NODE_TYPE) ? T->type_node.name : nullptr;

        bool handled = false;
        if (tname) {
            if (std::strncmp(tname, "texture_", 8) == 0) {
                SW_ParsedTextureMeta meta = sw_parse_texture_typenode(T);
                if (meta.is_storage) {
                    if (meta.is_array)      desc.type = storage_texture2d_array;
                    else if (meta.is_3d)    desc.type = storage_texture3d;
                    else                    desc.type = storage_texture2d;
                    desc.fstype = meta.fmt_or_sample;
                    desc.access = meta.access;
                } else {
                    if (meta.is_array)      desc.type = texture2d_array;
                    else if (meta.is_3d)    desc.type = texture3d;
                    else                    desc.type = texture2d;
                    desc.fstype = meta.fmt_or_sample; // sample type
                }
                handled = true;
            } else if (std::strncmp(tname, "sampler", 7) == 0) {
                desc.type = texture_sampler;
                handled = true;
            }
        }

        if (!handled) {
            if (gv.address_space && std::strcmp(gv.address_space, "uniform") == 0) {
                desc.type = uniform_buffer;
            } else if (gv.address_space && std::strcmp(gv.address_space, "storage") == 0) {
                desc.type = storage_buffer;
                desc.access = readwrite;
            } else {
                continue; // non-bindable
            }
        }
        BindingIdentifier identifier = {
            .length = (uint32_t)strlen(s.name),
        };
        if(identifier.length <= MAX_BINDING_NAME_LENGTH){
            memcpy(identifier.name, s.name, MAX_BINDING_NAME_LENGTH);
        }
        StringToUniformMap_put(out, identifier, desc);
    }

    wgsl_resolve_free((void*)syms);
    wgsl_resolver_free(R);
    wgsl_free_ast(ast);
    return out;
}

#endif // simple backend

// =================================================================================================
// TINT BACKEND
// Enabled when SUPPORT_TINT_WGSL_PARSER == 1
// =================================================================================================
#if defined(SUPPORT_TINT_WGSL_PARSER) && (SUPPORT_TINT_WGSL_PARSER == 1)

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

InOutAttributeInfo getAttributesWGSL(ShaderSources sources) {
#if defined(SUPPORT_TINT_WGSL_PARSER) && (SUPPORT_TINT_WGSL_PARSER == 1)
    return getAttributesWGSL_Tint(sources);
#else
    return getAttributesWGSL_Simple(sources);
#endif
}

StringToUniformMap* getBindingsWGSL(ShaderSources sources) {
#if defined(SUPPORT_TINT_WGSL_PARSER) && (SUPPORT_TINT_WGSL_PARSER == 1)
    return getBindingsWGSL_Tint(sources);
#else
    return getBindingsWGSL_Simple(sources);
#endif
}

EntryPointSet getEntryPointsWGSL(const char* shaderSourceWGSL) {
#if defined(SUPPORT_TINT_WGSL_PARSER) && (SUPPORT_TINT_WGSL_PARSER == 1)
    return getEntryPointsWGSL_Tint(shaderSourceWGSL);
#else
    return getEntryPointsWGSL_Simple(shaderSourceWGSL);
#endif
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
StringToUniformMap* getBindings(ShaderSources sources){
    
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

