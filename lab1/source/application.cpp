#include "application.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <vector>

namespace application {
namespace {
struct Vertex { glm::vec3 position; glm::vec3 color; };
struct Push { glm::mat4 mvp; };
VkPipelineLayout layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
VkBuffer vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_allocation = VK_NULL_HANDLE;
uint32_t vertex_count = 0;
float rotation[3] = {0.45f, 0.6f, 0.0f};
float scale = 1.0f;
int projection = 0;
bool auto_rotate = true;

std::vector<Vertex> makeDodecahedron() {
    constexpr float phi = 1.61803398875f;
    std::vector<glm::vec3> points;
    for (int a : {-1, 1}) for (int b : {-1, 1}) for (int c : {-1, 1}) points.emplace_back(float(a), float(b), float(c));
    for (int a : {-1, 1}) for (int b : {-1, 1}) points.emplace_back(0.0f, float(a)/phi, float(b)*phi);
    for (int a : {-1, 1}) for (int b : {-1, 1}) points.emplace_back(float(a)/phi, float(b)*phi, 0.0f);
    for (int a : {-1, 1}) for (int b : {-1, 1}) points.emplace_back(float(a)*phi, 0.0f, float(b)/phi);

    std::vector<std::vector<int>> faces;
    for (int i=0; i<20; ++i) for (int j=i+1; j<20; ++j) for (int k=j+1; k<20; ++k) {
        glm::vec3 n = glm::cross(points[j]-points[i], points[k]-points[i]);
        float len = glm::length(n); if (len < 1e-5f) continue; n /= len;
        bool pos=false, neg=false;
        for (int q=0; q<20; ++q) { float d=glm::dot(n,points[q]-points[i]); pos |= d>1e-4f; neg |= d< -1e-4f; }
        if (pos && neg) continue;
        if (glm::dot(n,points[i]) < 0) n=-n;
        std::vector<int> face;
        for (int q=0; q<20; ++q) if (std::abs(glm::dot(n,points[q]-points[i])) < 1e-4f) face.push_back(q);
        if (face.size()!=5) continue;
        std::sort(face.begin(),face.end());
        if (std::find(faces.begin(),faces.end(),face)==faces.end()) faces.push_back(face);
    }
    std::vector<Vertex> out;
    const std::array<glm::vec3,6> colors={glm::vec3(.98f,.32f,.25f),{.98f,.66f,.22f},{.28f,.78f,.55f},{.23f,.66f,.95f},{.66f,.43f,.95f},{.95f,.38f,.69f}};
    for (size_t fi=0; fi<faces.size(); ++fi) {
        auto face=faces[fi]; glm::vec3 center(0);
        for(int id:face) center+=points[id]; center/=5.0f;
        glm::vec3 normal=glm::normalize(center);
        glm::vec3 u=glm::normalize(points[face[0]]-center), v=glm::cross(normal,u);
        std::sort(face.begin(),face.end(),[&](int a,int b){
            auto pa=points[a]-center,pb=points[b]-center;
            return std::atan2(glm::dot(pa,v),glm::dot(pa,u)) < std::atan2(glm::dot(pb,v),glm::dot(pb,u));
        });
        if(glm::dot(glm::cross(points[face[1]]-points[face[0]],points[face[2]]-points[face[0]]),normal)<0) std::reverse(face.begin(),face.end());
        const auto color=colors[fi%colors.size()];
        for(int t=1;t<4;++t) for(int id:{face[0],face[t],face[t+1]}) out.push_back({points[id]*0.48f,color});
    }
    return out;
}

VkShaderModule loadShader(const char* path) {
    FILE* f=std::fopen(path,"rb"); if(!f) return VK_NULL_HANDLE;
    std::fseek(f,0,SEEK_END); long size=std::ftell(f); std::rewind(f);
    std::vector<uint32_t> code(size>0?size/4:0); bool ok=size>0 && std::fread(code.data(),1,size,f)==size; std::fclose(f);
    if(!ok) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize=size; ci.pCode=code.data();
    VkShaderModule m=VK_NULL_HANDLE; if(vkCreateShaderModule(graphics::internal::context.device,&ci,nullptr,&m)!=VK_SUCCESS) return VK_NULL_HANDLE; return m;
}
}

bool initialize() {
    auto& c=graphics::internal::context;
    auto vertices=makeDodecahedron(); vertex_count=uint32_t(vertices.size());
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=vertices.size()*sizeof(Vertex); bi.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{}; ai.usage=VMA_MEMORY_USAGE_AUTO; ai.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if(vmaCreateBuffer(c.allocator,&bi,&ai,&vertex_buffer,&vertex_allocation,&info)!=VK_SUCCESS) return false;
    std::memcpy(info.pMappedData,vertices.data(),bi.size);

    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(Push)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; li.pushConstantRangeCount=1; li.pPushConstantRanges=&range;
    if(vkCreatePipelineLayout(c.device,&li,nullptr,&layout)!=VK_SUCCESS) return false;
    VkShaderModule vs=loadShader("shaders/dodecahedron.vert.spv"), fs=loadShader("shaders/dodecahedron.frag.spv");
    if(!vs||!fs) { std::cerr<<"Shader files missing: run the CMake build from project root.\n"; return false; }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main",nullptr};
    stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",nullptr};
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},{1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,color)}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding; vi.vertexAttributeDescriptionCount=2; vi.pVertexAttributeDescriptions=attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask=0xf;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; bs.attachmentCount=1; bs.pAttachments=&blend;
    VkDynamicState dyns[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dyn.dynamicStateCount=2; dyn.pDynamicStates=dyns;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia; pi.pViewportState=&vp; pi.pRasterizationState=&rs; pi.pMultisampleState=&ms; pi.pDepthStencilState=&ds; pi.pColorBlendState=&bs; pi.pDynamicState=&dyn; pi.layout=layout; pi.renderPass=c.render_pass; pi.subpass=0;
    VkResult result=vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline);
    vkDestroyShaderModule(c.device,vs,nullptr); vkDestroyShaderModule(c.device,fs,nullptr);
    return result==VK_SUCCESS;
}

void shutdown() {
    auto& c=graphics::internal::context; vkQueueWaitIdle(c.graphics_queue);
    if(pipeline) vkDestroyPipeline(c.device,pipeline,nullptr); if(layout) vkDestroyPipelineLayout(c.device,layout,nullptr);
    if(vertex_buffer) vmaDestroyBuffer(c.allocator,vertex_buffer,vertex_allocation);
}

void update(double time) {
    ImGui::Begin("Правильный додекаэдр | вариант 10");
    ImGui::Text("12 пятиугольных граней, 20 вершин");
    const char* modes[]={"Перспективная","Ортографическая"}; ImGui::Combo("Проекция",&projection,modes,2);
    ImGui::SliderFloat3("Поворот",rotation,-3.14f,3.14f,"%.2f рад"); ImGui::SliderFloat("Масштаб",&scale,.35f,1.6f);
    ImGui::Checkbox("Вращать автоматически",&auto_rotate);
    if(ImGui::Button("Сбросить")) { rotation[0]=.45f; rotation[1]=.6f; rotation[2]=0; scale=1; }
    ImGui::Text("Vulkan + GLFW + ImGui"); ImGui::End();
    if(auto_rotate) rotation[1]=float(time)*.45f;
}

void render(const graphics::internal::FrameData& fd) {
    if(!pipeline||!fd.command_buffer) return;
    auto& c=graphics::internal::context; auto cmd=fd.command_buffer;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd,&begin);
    VkClearValue clears[2]{}; clears[0].color={{.035f,.045f,.07f,1.0f}}; clears[1].depthStencil={1.0f,0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rp.renderPass=c.render_pass; rp.framebuffer=fd.framebuffer; rp.renderArea={{0,0},c.swapchain_extent}; rp.clearValueCount=2; rp.pClearValues=clears;
    vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,float(c.swapchain_extent.width),float(c.swapchain_extent.height),0,1}; VkRect2D scissor{{0,0},c.swapchain_extent}; vkCmdSetViewport(cmd,0,1,&viewport); vkCmdSetScissor(cmd,0,1,&scissor);
    glm::mat4 model=glm::rotate(glm::mat4(1),rotation[0],{1,0,0}); model=glm::rotate(model,rotation[1],{0,1,0}); model=glm::rotate(model,rotation[2],{0,0,1}); model=glm::scale(model,glm::vec3(scale));
    float aspect=float(c.swapchain_extent.width)/std::max(1u,c.swapchain_extent.height);
    glm::mat4 proj=projection==0?glm::perspective(glm::radians(45.0f),aspect,.1f,20.0f):glm::ortho(-1.8f*aspect,1.8f*aspect,-1.8f,1.8f,.1f,20.0f);
    proj[1][1]*=-1; glm::mat4 view=glm::lookAt(glm::vec3(0,0,3.4f),glm::vec3(0),glm::vec3(0,1,0)); Push push{proj*view*model};
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); VkDeviceSize offset=0; vkCmdBindVertexBuffers(cmd,0,1,&vertex_buffer,&offset); vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push); vkCmdDraw(cmd,vertex_count,1,0,0);
    vkCmdEndRenderPass(cmd); vkEndCommandBuffer(cmd);
}
} // namespace application
