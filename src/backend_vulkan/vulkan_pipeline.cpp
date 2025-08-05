#include "config.h"
#include "wgvk.h"
#include <external/volk.h>
#define Font rlFont
#include <raygpu.h>
#undef Font
#include <internals.hpp>
#include <vulkan/vulkan_core.h>
#include "pipeline.h"
#include "vulkan_internals.hpp"
#include <spirv_reflect.h>
//#include <enum_translation.h>

#include <wgvk_structs_impl.h>

extern "C" DescribedShaderModule LoadShaderModuleSPIRV(ShaderSources sources){
    DescribedShaderModule ret zeroinit;
    
    for(uint32_t i = 0;i < sources.sourceCount;i++){
        VkShaderModuleCreateInfo csCreateInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            NULL,
            0,
            sources.sources[i].sizeInBytes,
            (const uint32_t*)sources.sources[i].data
        };

        VkWriteDescriptorSet ws;
        WGPUShaderModule insert = (WGPUShaderModule)RL_CALLOC(1, sizeof(WGPUShaderModuleImpl));
        VkShaderModule insert_ zeroinit;
        vkCreateShaderModule(g_vulkanstate.device->device, &csCreateInfo, nullptr, &insert_);
        insert->vulkanModuleMultiEP = insert_;
        spv_reflect::ShaderModule module(csCreateInfo.codeSize, csCreateInfo.pCode);
        uint32_t epCount = module.GetEntryPointCount();
        for(uint32_t i = 0;i < epCount;i++){
            SpvReflectShaderStageFlagBits epStage = module.GetEntryPointShaderStage(i);
            WGPUShaderStageEnum stage = [](SpvReflectShaderStageFlagBits epStage){
                switch(epStage){
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
                        return WGPUShaderStageEnum_Vertex;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
                        return WGPUShaderStageEnum_Fragment;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
                        return WGPUShaderStageEnum_Compute;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
                        return WGPUShaderStageEnum_Geometry;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_KHR:
                        return WGPUShaderStageEnum_RayGen;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
                        return WGPUShaderStageEnum_ClosestHit;
                    case SpvReflectShaderStageFlagBits::SPV_REFLECT_SHADER_STAGE_MISS_BIT_KHR:
                        return WGPUShaderStageEnum_Miss;
                    default:
                        TRACELOG(LOG_FATAL, "Unknown shader stage: %d", (int)epStage);
                        return WGPUShaderStageEnum_Force32;
                }
            }(epStage);
            
            ret.reflectionInfo.ep[stage].stage = stage;
            ret.stages[stage].module = insert;
            std::memset(ret.reflectionInfo.ep[stage].name, 0, sizeof(ret.reflectionInfo.ep[stage].name));
            uint32_t eplength = std::strlen(module.GetEntryPointName(i));
            rassert(eplength < 16, "Entry point name must be < 16 chars");
            std::copy(module.GetEntryPointName(i), module.GetEntryPointName(i) + eplength, ret.reflectionInfo.ep[stage].name);
            //TRACELOG(LOG_INFO, "%s : %d", module.GetEntryPointName(i), module.GetEntryPointShaderStage(i));
        }
    }

    return ret;
}


static inline VkPrimitiveTopology toVulkanPrimitive(PrimitiveType type){
    switch(type){
        case RL_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case RL_TRIANGLES: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RL_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RL_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case RL_QUADS:
            //rassert(false, "Quads are not a primitive type");
        default:
            rg_unreachable();
    }
}
extern "C" WGPURenderPipeline createSingleRenderPipe(const ModifiablePipelineState& mst, const DescribedShaderModule& shaderModule, const DescribedBindGroupLayout& bglayout, const DescribedPipelineLayout& pllayout){
    TRACELOG(LOG_INFO, "Generating new single pipeline");
    WGPURenderPipelineDescriptor pipelineDesc zeroinit;
    const RenderSettings* settings = &mst.settings; 
    pipelineDesc.multisample.count = settings->sampleCount ? settings->sampleCount : 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;
    pipelineDesc.layout = (WGPUPipelineLayout)pllayout.layout;

    WGPUVertexState   vertexState   zeroinit;
    WGPUFragmentState fragmentState zeroinit;
    WGPUBlendState    blendState    zeroinit;

    vertexState.module = (WGPUShaderModule)shaderModule.stages[WGPUShaderStageEnum_Vertex].module;

    VertexBufferLayoutSet vlayout_complete = getBufferLayoutRepresentation(mst.vertexAttributes, mst.vertexAttributeCount);
    vertexState.bufferCount = vlayout_complete.number_of_buffers;

    std::vector<WGPUVertexBufferLayout> layouts_converted;
    for(uint32_t i = 0;i < vlayout_complete.number_of_buffers;i++){
        layouts_converted.push_back(WGPUVertexBufferLayout{
            .nextInChain    = nullptr,
            .stepMode       = (WGPUVertexStepMode)vlayout_complete.layouts[i].stepMode,
            .arrayStride    = vlayout_complete.layouts[i].arrayStride,
            .attributeCount = vlayout_complete.layouts[i].attributeCount,
            //TODO: this relies on the fact that VertexAttribute and WGPUVertexAttribute are exactly compatible
            .attributes     = (WGPUVertexAttribute*)vlayout_complete.layouts[i].attributes,
        });
    }
    vertexState.buffers = layouts_converted.data();
    vertexState.constantCount = 0;
    vertexState.entryPoint = WGPUStringView{shaderModule.reflectionInfo.ep[WGPUShaderStageEnum_Vertex].name, std::strlen(shaderModule.reflectionInfo.ep[WGPUShaderStageEnum_Fragment].name)};
    pipelineDesc.vertex = vertexState;


    
    fragmentState.module = shaderModule.stages[WGPUShaderStageEnum_Fragment].module;
    fragmentState.entryPoint = WGPUStringView{shaderModule.reflectionInfo.ep[WGPUShaderStageEnum_Fragment].name, std::strlen(shaderModule.reflectionInfo.ep[WGPUShaderStageEnum_Fragment].name)};
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    blendState.color.srcFactor = (WGPUBlendFactor   )settings->blendState.color.srcFactor;
    blendState.color.dstFactor = (WGPUBlendFactor   )settings->blendState.color.dstFactor;
    blendState.color.operation = (WGPUBlendOperation)settings->blendState.color.operation;
    blendState.alpha.srcFactor = (WGPUBlendFactor   )settings->blendState.alpha.srcFactor;
    blendState.alpha.dstFactor = (WGPUBlendFactor   )settings->blendState.alpha.dstFactor;
    blendState.alpha.operation = (WGPUBlendOperation)settings->blendState.alpha.operation;
    WGPUColorTargetState colorTargets[MAX_COLOR_ATTACHMENTS];

    for(uint32_t i = 0;i < mst.colorAttachmentState.colorAttachmentCount;i++){
        WGPUColorTargetState insert = {
            .format = toWGPUPixelFormat(mst.colorAttachmentState.attachmentFormats[i]),
            .blend = &blendState,
            .writeMask = WGPUColorWriteMask_All,
        };
        colorTargets[i] = insert;
    }
    fragmentState.targetCount = mst.colorAttachmentState.colorAttachmentCount;
    fragmentState.targets = colorTargets;
    pipelineDesc.fragment = &fragmentState;
    // We setup a depth buffer state for the render pipeline
    WGPUDepthStencilState depthStencilState;
    if(settings->depthTest){
        // Keep a fragment only if its depth is lower than the previously blended one
        // Each time a fragment is blended into the target, we update the value of the Z-buffer
        // Store the format in a variable as later parts of the code depend on it
        // Deactivate the stencil alltogether
        WGPUTextureFormat depthTextureFormat = WGPUTextureFormat_Depth32Float;
        depthStencilState = (WGPUDepthStencilState){
            .format = depthTextureFormat,
            .depthWriteEnabled = WGPUOptionalBool_True,
            .depthCompare = (WGPUCompareFunction)settings->depthCompare,
            .stencilFront = {.compare = WGPUCompareFunction_Always},
            .stencilBack = {.compare = WGPUCompareFunction_Always},
            .stencilReadMask = 0,
            .stencilWriteMask = 0,
        };
    }

    pipelineDesc.depthStencil = settings->depthTest ? &depthStencilState : nullptr;
    pipelineDesc.primitive.frontFace = (WGPUFrontFace)settings->frontFace;
    pipelineDesc.primitive.cullMode = settings->faceCull ? WGPUCullMode_Back : WGPUCullMode_None;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    WGPUPrimitiveLineWidthInfo lwinfo = {
        .chain = {
            .sType = WGPUSType_PrimitiveLineWidthInfo,
        },
        .lineWidth = mst.settings.lineWidth,
    };

    pipelineDesc.primitive.nextInChain = &lwinfo.chain;
    auto toWebGPUPrimitive = [](PrimitiveType pt){
        switch(pt){
            case RL_LINES: return WGPUPrimitiveTopology_LineList;
            case RL_TRIANGLES: return WGPUPrimitiveTopology_TriangleList;
            case RL_TRIANGLE_STRIP: return WGPUPrimitiveTopology_TriangleStrip;
            case RL_POINTS: return WGPUPrimitiveTopology_PointList;
            case RL_QUADS:
            default:
            rg_unreachable();
        }
    };
    pipelineDesc.primitive.topology = toWebGPUPrimitive(mst.primitiveType);
    return wgpuDeviceCreateRenderPipeline((WGPUDevice)GetDevice(), &pipelineDesc);
}

/*
extern "C" RenderPipelineQuartet GetPipelinesForLayoutSet(DescribedPipeline* ret, const VertexBufferLayoutSet vls){

    TRACELOG(LOG_INFO, "Generating new pipelines");

    auto& settings = ret->state;
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = (VkShaderModule)ret->shaderModule.stages[ShaderStage_Vertex].module;
    vertShaderStageInfo.pName = ret->shaderModule.reflectionInfo.ep[ShaderStage_Vertex].name;

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = (VkShaderModule)ret->shaderModule.stages[ShaderStage_Fragment].module;
    fragShaderStageInfo.pName = ret->shaderModule.reflectionInfo.ep[ShaderStage_Fragment].name;

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex Input Setup
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    auto [vad, vbd] = genericVertexLayoutSetToVulkan(vls);

    vertexInputInfo.vertexBindingDescriptionCount = vbd.size();
    vertexInputInfo.vertexAttributeDescriptionCount = vad.size();
    vertexInputInfo.pVertexAttributeDescriptions = vad.data();
    vertexInputInfo.pVertexBindingDescriptions = vbd.data();

    // Input Assembly Setup
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and Scissor Setup
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkRect2D scissor{0, 0, g_vulkanstate.surface.surfaceConfig.width, g_vulkanstate.surface.surfaceConfig.height};
    VkViewport fullView{
        0.0f, 
        (float)g_vulkanstate. surface.surfaceConfig.height, 
        (float)g_vulkanstate. surface.surfaceConfig.width, 
        -((float)g_vulkanstate.surface.surfaceConfig.height), 
        0.0f, 
        1.0f
    };
    viewportState.pScissors = &scissor;
    viewportState.pViewports = &fullView;

    // Rasterization State Setup
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;

    // Incorporate face culling from RenderSettings
    if (settings.faceCull) {
        rasterizer.cullMode = VK_CULL_MODE_NONE;//VK_CULL_MODE_BACK_BIT; // You can make this configurable
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }

    // Set front face based on RenderSettings
    rasterizer.frontFace = toVulkanFrontFace(settings.frontFace);

    rasterizer.depthBiasEnable = VK_FALSE;

    // Depth Stencil State Setup
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = settings.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = settings.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVulkanCompareFunction((CompareFunction)settings.depthCompare);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {}; // Optional

    // Multisampling State Setup
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    
    // Map sampleCount from RenderSettings to VkSampleCountFlagBits
    switch (settings.sampleCount) {
        case 1: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; break;
        case 2: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_2_BIT; break;
        case 4: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; break;
        case 8: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT; break;
        case 16: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_16_BIT; break;
        case 32: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_32_BIT; break;
        case 64: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_64_BIT; break;
        default: multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; break;
    }

    // Color Blend Attachment Setup
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    // Enable blending based on whether blend operations are set
    bool blendingEnabled = 
        settings.blendOperationAlpha != BlendOperation_Add || 
        settings.blendFactorSrcAlpha != BlendFactor_One ||
        settings.blendFactorDstAlpha != BlendFactor_Zero ||
        settings.blendOperationColor != BlendOperation_Add ||
        settings.blendFactorSrcColor != BlendFactor_One ||
        settings.blendFactorDstColor != BlendFactor_Zero;

    colorBlendAttachment.blendEnable = blendingEnabled ? VK_TRUE : VK_FALSE;

    if (blendingEnabled) {
        // Configure blending for color
        colorBlendAttachment.srcColorBlendFactor = toVulkanBlendFactor(settings.blendFactorSrcColor);
        colorBlendAttachment.dstColorBlendFactor = toVulkanBlendFactor(settings.blendFactorDstColor);
        colorBlendAttachment.colorBlendOp =        toVulkanBlendOperation(settings.blendOperationColor);
        
        // Configure blending for alpha
        colorBlendAttachment.srcAlphaBlendFactor = toVulkanBlendFactor(settings.blendFactorSrcAlpha);
        colorBlendAttachment.dstAlphaBlendFactor = toVulkanBlendFactor(settings.blendFactorDstAlpha);
        colorBlendAttachment.alphaBlendOp =        toVulkanBlendOperation(settings.blendOperationAlpha);
    }

    // Color Blending State Setup
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 1.0f;
    colorBlending.blendConstants[1] = 1.0f;
    colorBlending.blendConstants[2] = 1.0f;
    colorBlending.blendConstants[3] = 1.0f;
    
    // Dynamic State Setup (optional based on RenderSettings)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, 
        VK_DYNAMIC_STATE_SCISSOR,
        //VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, 
        //VK_DYNAMIC_STATE_VERTEX_INPUT_EXT, 
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    
    // You can make dynamic states configurable via RenderSettings if needed
    dynamicState.dynamicStateCount = static_cast<uint32_t>(2);
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Pipeline Layout Setup
    
    
    // Graphics Pipeline Creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = settings.depthTest ? &depthStencil : nullptr; // Enable depth stencil if needed
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = (VkPipelineLayout)ret->layout.layout;

    RenderPassLayout rpLayout zeroinit;
    rpLayout.colorAttachmentCount = 1;
    rpLayout.depthAttachmentPresent = settings.depthTest;
    
    rpLayout.colorAttachments[0].format = toVulkanPixelFormat(BGRA8);
    rpLayout.colorAttachments[0].loadop = LoadOp_Load;
    rpLayout.colorAttachments[0].storeop = StoreOp_Store;
    rpLayout.colorAttachments[0].sampleCount = settings.sampleCount;
    
    if(settings.depthTest){
        rpLayout.depthAttachment.format = toVulkanPixelFormat(Depth32);
        rpLayout.depthAttachment.loadop = LoadOp_Load;
        rpLayout.depthAttachment.storeop = StoreOp_Store;
        rpLayout.depthAttachment.sampleCount = settings.sampleCount;
    }
    if(settings.sampleCount > 1){
        rpLayout.colorAttachments[1].format = toVulkanPixelFormat(BGRA8);
        rpLayout.colorAttachments[1].loadop = LoadOp_Load;
        rpLayout.colorAttachments[1].storeop = StoreOp_Store;
        rpLayout.colorAttachments[1].sampleCount = 1;
        rpLayout.colorResolveIndex = 1;
    }
    else{
        rpLayout.colorResolveIndex = VK_ATTACHMENT_UNUSED;
    }
    #if VULKAN_USE_DYNAMIC_RENDERING == 1
    VkPipelineRenderingCreateInfo rci zeroinit;
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    VkFormat colorAttachmentFormat = VK_FORMAT_B8G8R8A8_UNORM;
    rci.pColorAttachmentFormats = &colorAttachmentFormat;
    rci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    pipelineInfo.pNext = &rci;
    #else
    VkRenderPass rp = LoadRenderPassFromLayout(g_vulkanstate.device, rpLayout);
    pipelineInfo.renderPass = rp;
    pipelineInfo.subpass = 0;
    #endif
    
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    RenderPipelineQuartet quartet{
        .pipeline_TriangleList = callocnew(WGPURenderPipelineImpl),
        .pipeline_TriangleStrip = callocnew(WGPURenderPipelineImpl),
        .pipeline_LineList = callocnew(WGPURenderPipelineImpl),
        .pipeline_PointList = callocnew(WGPURenderPipelineImpl)
    };
    quartet.pipeline_TriangleList->layout = pipelineInfo.layout;
    quartet.pipeline_TriangleStrip->layout = pipelineInfo.layout;
    quartet.pipeline_LineList->layout = pipelineInfo.layout;
    quartet.pipeline_PointList->layout = pipelineInfo.layout;
    if (vkCreateGraphicsPipelines(g_vulkanstate.device->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, (VkPipeline*)&quartet.pipeline_TriangleList->renderPipeline) != VK_SUCCESS) {
        TRACELOG(LOG_FATAL, "Trianglelist pipiline creation failed");
    }
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (vkCreateGraphicsPipelines(g_vulkanstate.device->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, (VkPipeline*)&quartet.pipeline_TriangleStrip->renderPipeline) != VK_SUCCESS) {
        TRACELOG(LOG_FATAL, "Trianglelist pipiline creation failed");
    }
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (vkCreateGraphicsPipelines(g_vulkanstate.device->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, (VkPipeline*)&quartet.pipeline_LineList->renderPipeline) != VK_SUCCESS) {
        TRACELOG(LOG_FATAL, "Trianglelist pipiline creation failed");
    }
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (vkCreateGraphicsPipelines(g_vulkanstate.device->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, (VkPipeline*)&quartet.pipeline_PointList->renderPipeline) != VK_SUCCESS) {
        TRACELOG(LOG_FATAL, "Trianglelist pipiline creation failed");
    }
    ret->createdPipelines->pipelines.emplace(vls, quartet);

    return quartet;
}
*/

//extern "C" void UpdatePipeline(DescribedPipeline *pl){
//    pl->quartet = GetPipelinesForLayoutSet(pl, pl->vertexLayout);
//}

//extern "C" RenderPipelineQuartet GetPipelinesForLayout(DescribedPipeline *ret, const std::vector<AttributeAndResidence>& attribs){
//    
//    VertexBufferLayoutSet layoutset = getBufferLayoutRepresentation(attribs.data(), attribs.size());
//
//    auto ait = ret->createdPipelines->pipelines.find(layoutset);
//    if(ait != ret->createdPipelines->pipelines.end()){
//        UnloadBufferLayoutSet(layoutset);
//        return ait->second;
//    }
//    return GetPipelinesForLayoutSet(ret, layoutset);
//}

extern "C" DescribedPipeline* LoadPipelineEx(const char* shaderSource, const AttributeAndResidence* attribs, uint32_t attribCount, const ResourceTypeDescriptor* uniforms, uint32_t uniformCount, RenderSettings settings){
    ShaderSources sources = dualStage(shaderSource, sourceTypeWGSL, WGPUShaderStageEnum_Vertex, WGPUShaderStageEnum_Fragment);
    
    DescribedShaderModule mod = LoadShaderModule(sources);
    //std::unordered_map<std::string, std::pair<VertexFormat, uint32_t>> attribs = getAttributes(shaderSource);
    return LoadPipelineMod(mod, attribs, attribCount, uniforms, uniformCount, settings);
}
extern "C" DescribedPipeline* LoadPipeline(const char* shaderSource){
    ShaderSources sources = dualStage(shaderSource, sourceTypeWGSL, WGPUShaderStageEnum_Vertex, WGPUShaderStageEnum_Fragment);
    auto [attribs, attachments] = getAttributesWGSL(sources);
    std::vector<AttributeAndResidence> allAttribsInOneBuffer;
    
    allAttribsInOneBuffer.reserve(attribs.size());
    uint32_t offset = 0;
    for(const auto& [name, attr] : attribs){
        const auto& [format, location] = attr;
        allAttribsInOneBuffer.push_back(AttributeAndResidence{
            .attr = WGPUVertexAttribute{
                .nextInChain = nullptr,
                .format = format,
                .offset = offset,
                .shaderLocation = location
            },
            .bufferSlot = 0,
            .stepMode = WGPUVertexStepMode_Vertex,
            .enabled = true}
        );
        offset += attributeSize(format);
    }
    

    auto bindings = getBindingsWGSL(sources);

    std::vector<ResourceTypeDescriptor> values;
    values.reserve(bindings.size());
    for(const auto& [x,y] : bindings){
        values.push_back(y);
    }
    std::sort(values.begin(), values.end(),[](const ResourceTypeDescriptor& x, const ResourceTypeDescriptor& y){
        return x.location < y.location;
    });
    return LoadPipelineEx(shaderSource, allAttribsInOneBuffer.data(), allAttribsInOneBuffer.size(), values.data(), values.size(), GetDefaultSettings());
}

//extern "C" void UpdatePipelineWithNewLayout(DescribedPipeline* ret, const std::vector<AttributeAndResidence>& attributes){
//    ret->state.vertexAttributes = attributes;
//    ret->activePipeline = ret->pipelineCache.getOrCreate(ret->state, ret->shaderModule, ret->bglayout, ret->layout);
//}

extern "C" DescribedPipeline* LoadPipelineMod(DescribedShaderModule mod, const AttributeAndResidence* attribs, uint32_t attribCount, const ResourceTypeDescriptor* uniforms, uint32_t uniformCount, RenderSettings settings){
    DescribedPipeline* ret = callocnewpp(DescribedPipeline);
    ret->state.settings = settings;
    ret->state.vertexAttributes = (AttributeAndResidence*)attribs; 
    ret->state.vertexAttributeCount = attribCount; 
    ret->bglayout = LoadBindGroupLayout(uniforms, uniformCount, false);
    ret->shaderModule = mod;
    ret->state.colorAttachmentState.colorAttachmentCount = mod.reflectionInfo.colorAttachmentCount;

    std::fill(ret->state.colorAttachmentState.attachmentFormats, ret->state.colorAttachmentState.attachmentFormats  + ret->state.colorAttachmentState.colorAttachmentCount, BGRA8);
    //auto [spirV, spirF] = glsl_to_spirv(vsSource, fsSource);
    //ret->sh = LoadShaderModuleFromSPIRV_Vk(spirV.data(), spirV.size() * 4, spirF.data(), spirF.size() * 4);
    
    WGPUPipelineLayoutDescriptor pldesc zeroinit;
    pldesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout bgls[1] = {ret->bglayout.layout};
    pldesc.bindGroupLayouts = bgls;

    ret->layout.layout = wgpuDeviceCreatePipelineLayout(g_vulkanstate.device, &pldesc);
    std::vector<WGPUBindGroupEntry> bge(uniformCount);

    for(uint32_t i = 0;i < bge.size();i++){
        bge[i] = WGPUBindGroupEntry{};
        bge[i].binding = uniforms[i].location;
    }
    ret->bindGroup = LoadBindGroup(&ret->bglayout, bge.data(), bge.size());
    return ret;
}

extern "C" DescribedPipeline* LoadPipelineForVAOEx(ShaderSources sources, VertexArray* vao, const ResourceTypeDescriptor* uniforms, uint32_t uniformCount, RenderSettings settings){
    //detectShaderLanguage()
    
    DescribedShaderModule module = LoadShaderModule(sources);
    
    DescribedPipeline* pl = LoadPipelineMod(module, vao->attributes, vao->attributes_count, uniforms, uniformCount, settings);
    //DescribedPipeline* pl = LoadPipelineEx(shaderSource, nullptr, 0, uniforms, uniformCount, settings);
    PreparePipeline(pl, vao);
    return pl;
}

extern "C" void UpdateBindGroupEntry(DescribedBindGroup* bg, size_t location, WGPUBindGroupEntry entry){

    WGPUBindGroup bgImpl = (WGPUBindGroup)bg->bindGroup;
    uint32_t index = ~0u;
    for(uint32_t i = 0;i < bg->entryCount;i++){
        if(bg->entries[i].binding == location){
            index = i; break;
        }
    }
    rassert(index != ~0u, "No entry was found with given location");

    auto& newpuffer = entry.buffer;
    auto& newtexture = entry.textureView;
    if(newtexture && bg->entries[index].textureView == newtexture){
        //return;
    }
    uint64_t oldHash = bg->descriptorHash;
    if(bg->entries[index].buffer){
        WGPUBuffer wBuffer = (WGPUBuffer)bg->entries[index].buffer;
        wgpuBufferRelease(wBuffer);
    }
    else if(bg->entries[index].textureView){
        //TODO: currently not the case anyway, but this is nadinöf
        wgpuTextureViewRelease((WGPUTextureView)bg->entries[index].textureView);
    }
    else if(bg->entries[index].sampler){
        // TODO
    }
    if(entry.buffer){
        wgpuBufferAddRef(entry.buffer);
    }
    else if(entry.textureView){
        wgpuTextureViewAddRef(entry.textureView);
    }
    else if(entry.sampler){
        wgpuSamplerAddRef(entry.sampler);
    }
    else{
        TRACELOG(LOG_FATAL, "Invalid ResourceDescriptor");
    }

    bg->entries[index] = entry;
    //bg->descriptorHash ^= bgEntryHash(bg->entries[index]);
    WGPUBindGroup wB = (WGPUBindGroup)bg->bindGroup;
    

    if(bg->bindGroup && wB->refCount > 1){
        wgpuBindGroupRelease(wB);
        bg->bindGroup = nullptr;
    }


    //else if(!bg->needsUpdate && bg->bindGroup){
    //    g_wgpustate.bindGroupPool[oldHash] = bg->bindGroup;
    //    bg->bindGroup = nullptr;
    //}
    bg->needsUpdate = true;
    
    //bg->bindGroup = wgpuDeviceCreateBindGroup(GetDevice(), &(bg->desc));
}

void UpdateBindGroup(DescribedBindGroup* bg){
    const auto* layout = bg->layout;
    const auto* layoutlayout = layout->layout;
    rassert(layout != nullptr, "DescribedBindGroupLayout is nullptr");
    rassert(layoutlayout != nullptr, "WGPUBindGroupLayout is nullptr");
    if(bg->needsUpdate == false)return;
    WGPUBindGroupDescriptor bgdesc zeroinit;

    bgdesc.entryCount = bg->entryCount;
    bgdesc.entries = bg->entries;
    WGPUBindGroupLayout wvl = bg->layout->layout;
    bgdesc.layout = wvl;
    //std::vector<ResourceTypeDescriptor> ldtypes(wvl->entries, wvl->entries + wvl->entryCount);
    //bgdesc.entries 
    if(bg->bindGroup && ((WGPUBindGroup)bg->bindGroup)->refCount == 1){  
        WGPUBindGroup writeTo = (WGPUBindGroup)bg->bindGroup;      
        wgpuWriteBindGroup(g_vulkanstate.device, writeTo, &bgdesc);
    }
    else{
        if(bg->bindGroup){
            TRACELOG(LOG_WARNING, "Weird. This shouldn't be the case");
            wgpuBindGroupRelease((WGPUBindGroup)bg->bindGroup);
        }
        bg->bindGroup = wgpuDeviceCreateBindGroup(g_vulkanstate.device, &bgdesc);
    }
    bg->needsUpdate = false;
}


//TODO: actually, one would need to iterate entries to find out where .binding == binding
void SetBindGroupTexture_Vk(DescribedBindGroup* bg, uint32_t binding, Texture tex){

    bg->entries[binding].textureView = (WGPUTextureView)tex.view;
    if(bg->bindGroup){
        wgpuBindGroupRelease((WGPUBindGroup)bg->bindGroup);
        bg->bindGroup = nullptr;
    }
    bg->needsUpdate = true;
}
void SetBindGroupBuffer_Vk(DescribedBindGroup* bg, uint32_t binding, DescribedBuffer* buf){
    
    //TODO: actually, one would need to iterate entries to find out where .binding == binding
    bg->entries[binding].buffer = (WGPUBuffer)buf->buffer;
    if(bg->bindGroup){
        wgpuBindGroupRelease((WGPUBindGroup)bg->bindGroup);
        bg->bindGroup = nullptr;
    }
    bg->needsUpdate = true;
}
void SetBindGroupSampler_Vk(DescribedBindGroup* bg, uint32_t binding, DescribedSampler buf){
    
    //TODO: actually, one would need to iterate entries to find out where .binding == binding
    bg->entries[binding].sampler = buf.sampler;

    if(bg->bindGroup){
        wgpuBindGroupRelease((WGPUBindGroup)bg->bindGroup);
        bg->bindGroup = nullptr;
    }
    bg->needsUpdate = true;
}


extern "C" DescribedComputePipeline* LoadComputePipelineEx(const char* shaderCode, const ResourceTypeDescriptor* uniforms, uint32_t uniformCount){
    DescribedComputePipeline* ret = callocnew(DescribedComputePipeline);
    ShaderSources sources = singleStage(shaderCode, detectShaderLanguage(shaderCode, std::strlen(shaderCode)), WGPUShaderStageEnum_Compute);
    DescribedShaderModule computeShaderModule = LoadShaderModule(sources);
    
    
    DescribedBindGroupLayout bgl = LoadBindGroupLayout(uniforms, uniformCount, true);
    VkPipelineLayoutCreateInfo lci zeroinit;
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &(reinterpret_cast<WGPUBindGroupLayout>(bgl.layout)->layout);
    VkPipelineLayout layout zeroinit;
    VkResult pipelineCreationResult = vkCreatePipelineLayout(g_vulkanstate.device->device, &lci, nullptr, &layout);
    VkPipelineShaderStageCreateInfo computeStage {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    if(computeShaderModule.stages[WGPUShaderStageEnum_Compute].module->vulkanModuleMultiEP)
        computeStage.module = computeShaderModule.stages[WGPUShaderStageEnum_Compute].module->vulkanModuleMultiEP;
    else
        computeStage.module = computeShaderModule.stages[WGPUShaderStageEnum_Compute].module->modules[WGPUShaderStageEnum_Compute].module;
    
    computeStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeStage.pName = computeShaderModule.reflectionInfo.ep[WGPUShaderStageEnum_Compute].name;
    
    const VkComputePipelineCreateInfo cpci{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = computeStage,
        .layout = layout,
        .basePipelineHandle = nullptr, 
        .basePipelineIndex = 0
    };
    
    WGPUComputePipeline retpipeline = callocnewpp(WGPUComputePipelineImpl);
    ret->pipeline = retpipeline;
    WGPUPipelineLayout retlayout = callocnewpp(WGPUPipelineLayoutImpl);
    retlayout->refCount = 2;
    retlayout->layout = layout;
    vkCreateComputePipelines(g_vulkanstate.device->device, nullptr, 1, &cpci, nullptr, &retpipeline->computePipeline);
    ret->layout = retlayout;
    retpipeline->layout = retlayout;
    ret->bglayout = bgl;
    std::vector<WGPUBindGroupEntry> bge(uniformCount);
    
    for(uint32_t i = 0;i < bge.size();i++){
        bge[i] = WGPUBindGroupEntry{};
        bge[i].binding = uniforms[i].location;
    }
    ret->bindGroup = LoadBindGroup(&ret->bglayout, bge.data(), bge.size());
    ret->shaderModule = computeShaderModule;
    ret->layout = retlayout;
    return ret;
}

extern "C" DescribedComputePipeline* LoadComputePipeline(const char* shaderCode){
    ShaderSources source = singleStage(shaderCode, detectShaderLanguage(shaderCode, std::strlen(shaderCode)), WGPUShaderStageEnum_Compute);
    std::unordered_map<std::string, ResourceTypeDescriptor> bindings = getBindings(source);
    std::vector<ResourceTypeDescriptor> values;
    values.reserve(bindings.size());
    for(const auto& [x,y] : bindings){
        values.push_back(y);
    }
    std::sort(values.begin(), values.end(),[](const ResourceTypeDescriptor& x, const ResourceTypeDescriptor& y){
        return x.location < y.location;
    });
    return LoadComputePipelineEx(shaderCode, values.data(), values.size());
}

//DescribedShaderModule LoadShaderModuleWGSL(ShaderSources sourcesWGSL){
//    ShaderSources spirv = wgsl_to_spirv(sourcesWGSL);
//    DescribedShaderModule mod = LoadShaderModuleSPIRV(spirv);
//    mod.reflectionInfo.uniforms = callocnewpp(StringToUniformMap);
//    mod.reflectionInfo.uniforms->uniforms = getBindings(sourcesWGSL);
//    return mod;
//}
