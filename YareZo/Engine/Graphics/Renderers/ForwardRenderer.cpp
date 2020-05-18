#include "Graphics/Renderers/ForwardRenderer.h"

#include "Platform/Vulkan/Vk_Utilities.h"

#include "Core/Yzh.h"
#include "Core/Memory.h"

#include <glm/gtx/string_cast.hpp>

namespace Yarezo::Graphics {

    ForwardRenderer::ForwardRenderer() {
        //m_Models.emplace_back(new Model("../YareZo/Resources/Models/chalet.obj", "../YareZo/Resources/Textures/chalet.jpg"));
        m_Models.emplace_back(new Model("../YareZo/Resources/Models/viking_room.obj",  "../YareZo/Resources/Textures/viking_room.png"));
        m_Models.emplace_back(new Model("../YareZo/Resources/Models/cube.obj", "../YareZo/Resources/Textures/crate.png"));
        init();
    }

    ForwardRenderer::~ForwardRenderer() {
        cleanupSwapChain();

        for (auto model : m_Models){
            delete model;
        }
        delete m_DefaultMaterial;

        delete m_Renderer;
    }

    void ForwardRenderer::cleanupSwapChain() {

        delete m_DepthBuffer;

        for (int i = (int)m_FrameBuffers.size() - 1; i >= 0; i--) {
            m_FrameBuffers[i]->cleanUp();
            delete m_FrameBuffers[i];
            m_FrameBuffers.pop_back();
        }

        for (auto commandBuffer : m_CommandBuffers) {
            commandBuffer->cleanUp();
            delete commandBuffer;
        }

        if (m_UboDynamicData.model) {
            alignedFree(m_UboDynamicData.model);
        }

        m_Pipelines.pipeline->cleanUp();
        delete m_Pipelines.pipeline;

        m_Pipelines.skybox->cleanUp();
        delete m_Pipelines.skybox;


        m_RenderPass->cleanUp();
        delete m_RenderPass;

        delete m_UniformBuffers.view;
        delete m_UniformBuffers.dynamic;
        delete m_UniformBuffers.skybox;

        delete m_SkyboxModel;
    }

    void ForwardRenderer::init() {
        m_Renderer = new YzVkRenderer();
        m_Renderer->init();

        for (auto model: m_Models) {
            model->load(MaterialTexType::Texture2D);
        }

        m_DefaultMaterial = new Material();

        m_WindowWidth =  m_Renderer->getYzSwapchain()->getExtent().width;
        m_WindowHeight = m_Renderer->getYzSwapchain()->getExtent().height;

        const RenderPassInfo renderPassInfo{ m_Renderer->getYzSwapchain()->getImageFormat() };
        m_RenderPass = new YzVkRenderPass();
        m_RenderPass->init(renderPassInfo);

        createGraphicsPipeline();

        createFrameBuffers();

        prepareUniformBuffers();

        createDescriptorSets();

        createCommandBuffers();


    }

    void ForwardRenderer::waitIdle() {
        YzVkDevice::instance()->waitIdle();
    }

    void ForwardRenderer::renderScene() {
        if (!m_Renderer->begin()) {
            recreateSwapChain();
        }

        m_CurrentBufferID = m_Renderer->getYzSwapchain()->getCurrentImage();

        begin();


        glm::mat4 model_transform = glm::mat4(1.0f);
        model_transform = glm::translate(model_transform, glm::vec3(0.0f, -0.15f, -1.0f));
        model_transform = glm::rotate(model_transform, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        model_transform = glm::rotate(model_transform, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        submitModel(m_Models[0], model_transform);

        glm::mat4 model_transform2 = glm::mat4(1.0f);
        model_transform2 = glm::translate(model_transform2, glm::vec3(0.0f, 0.0f, 1.0f));
        model_transform2 = glm::rotate(model_transform2, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        submitModel(m_Models[1], model_transform2);

        glm::mat4 model_transform3 = glm::mat4(1.0f);
        model_transform3 = glm::translate(model_transform3, glm::vec3(0.0f, 2.0f, 0.0f));

        //submitModel(m_Models[2], model_transform3);

        present();

        end();
    }

    void ForwardRenderer::submitModel(Model* model, const glm::mat4& transform) {
        RenderCommand renderCommand;
        renderCommand.model = model;
        renderCommand.transform = transform;

        m_CommandQueue.push_back(renderCommand);
    }

    void ForwardRenderer::present() {
        int index = 0;
        YzVkCommandBuffer* currentCommandBuffer = m_CommandBuffers[m_CurrentBufferID];

        if (showSkybox) {
            vkCmdBindDescriptorSets(currentCommandBuffer->getCommandBuffer(),
                                    VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipelines.skybox->getPipelineLayout(),
                                    0, 1, &m_DescriptorSets.skybox->getDescriptorSet(0), 0 , nullptr);
            m_SkyboxModel->getMesh()->getVertexBuffer()->bind(*currentCommandBuffer);
            m_SkyboxModel->getMesh()->getIndexBuffer()->bind(*currentCommandBuffer);
            m_Pipelines.skybox->setActive(*currentCommandBuffer);
            vkCmdDrawIndexed(currentCommandBuffer->getCommandBuffer(),
                             static_cast<uint32_t>(m_SkyboxModel->getMesh()->getIndexBuffer()->getSize() / sizeof(uint32_t)),
                             1, 0, 0, 0);
        }

        for (auto& command : m_CommandQueue) {

            updateUniformBuffers(index, command.transform);



            uint32_t dynamicOffset = index * static_cast<uint32_t>(m_DynamicAlignment);

            m_Pipelines.pipeline->setActive(*currentCommandBuffer);

            int imageIdx = command.model->getImageIdx();
            vkCmdPushConstants(currentCommandBuffer->getCommandBuffer(), m_Pipelines.pipeline->getPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), (void *)&imageIdx);

            vkCmdBindDescriptorSets(currentCommandBuffer->getCommandBuffer(),
                                    VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipelines.pipeline->getPipelineLayout(), 0u,
                                    1u, &m_DescriptorSets.descriptorSet->getDescriptorSet(0), 1, &dynamicOffset);

            command.model->getMesh()->getVertexBuffer()->bind(*currentCommandBuffer);
            command.model->getMesh()->getIndexBuffer()->bind(*currentCommandBuffer);

            vkCmdDrawIndexed(currentCommandBuffer->getCommandBuffer(), static_cast<uint32_t>(command.model->getMesh()->getIndexBuffer()->getSize() / sizeof(uint32_t)), 1, 0, 0, 0);
            index++;
        }
    }

    void ForwardRenderer::begin() {
        m_CommandQueue.clear();

        m_CommandBuffers[m_CurrentBufferID]->beginRecording();

        m_RenderPass->beginRenderPass(m_CommandBuffers[m_CurrentBufferID], m_FrameBuffers[m_CurrentBufferID], m_Renderer->getYzSwapchain().get());
    }

    void ForwardRenderer::end() {
        m_RenderPass->endRenderPass(m_CommandBuffers[m_CurrentBufferID]);

        m_CommandBuffers[m_CurrentBufferID]->endRecording();

        if (!m_Renderer->present(m_CommandBuffers[m_CurrentBufferID])) {
            recreateSwapChain();
        }
    }

    void ForwardRenderer::recreateSwapChain() {
        m_WindowWidth =  m_Renderer->getYzSwapchain()->getExtent().width;
        m_WindowHeight = m_Renderer->getYzSwapchain()->getExtent().height;

        cleanupSwapChain();
        RenderPassInfo renderPassInfo{ m_Renderer->getYzSwapchain()->getImageFormat() };
        m_RenderPass = new YzVkRenderPass();
        m_RenderPass->init(renderPassInfo);
        createGraphicsPipeline();
        createFrameBuffers();
        prepareUniformBuffers();
        createDescriptorSets();
        createCommandBuffers();
    }

    void ForwardRenderer::createGraphicsPipeline() {
        YzVkShader shader("../YareZo/Resources/Shaders", "texture_array.shader");

        PipelineInfo pipelineInfo = { &shader,  m_RenderPass, m_Renderer->getYzSwapchain().get() };
        pipelineInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        pipelineInfo.depthTestEnable = VK_TRUE;
        pipelineInfo.depthWriteEnable = VK_TRUE;
        pipelineInfo.maxObjects = 2;

        //                                       Binding, DescriptorType, DescriptorCount, StageFlags, pImmuatbleSamplers
        pipelineInfo.layoutBindings.emplace_back(VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                                              1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
        pipelineInfo.layoutBindings.emplace_back(VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                                                              1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
        pipelineInfo.layoutBindings.emplace_back(VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                                              MAX_NUM_TEXTURES, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});

        m_Pipelines.pipeline = new YzVkPipeline();
        m_Pipelines.pipeline->init(pipelineInfo);

        YzVkShader skyboxShader("../YareZo/Resources/Shaders", "skybox.shader");
        pipelineInfo.shader = &skyboxShader;
        pipelineInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
        pipelineInfo.depthTestEnable = VK_FALSE;
        pipelineInfo.depthWriteEnable = VK_FALSE;

        pipelineInfo.layoutBindings.clear();
        pipelineInfo.layoutBindings.emplace_back(VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                                              1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
        pipelineInfo.layoutBindings.emplace_back(VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                                              1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});


        m_Pipelines.skybox = new YzVkPipeline();
        m_Pipelines.skybox->init(pipelineInfo);

    }

    void ForwardRenderer::createFrameBuffers() {
        VkFormat depthFormat = VkUtil::findDepthFormat();
        m_DepthBuffer = YzVkImage::createDepthStencilBuffer(m_WindowWidth, m_WindowHeight, depthFormat);

        FramebufferInfo framebufferInfo;
        framebufferInfo.type = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.width = m_WindowWidth;
        framebufferInfo.height = m_WindowHeight;
        framebufferInfo.layers = 1;

        for (uint32_t i = 0; i < m_Renderer->getYzSwapchain()->getImageViewSize(); i++) {
            framebufferInfo.attachments = { m_Renderer->getYzSwapchain()->getImageView(i), m_DepthBuffer->getImageView() };
            m_FrameBuffers.push_back(new YzVkFramebuffer(framebufferInfo));
        }
    }

    void ForwardRenderer::createDescriptorSets() {
        size_t swapchainImagesSize = m_Renderer->getYzSwapchain()->getImagesSize();

        DescriptorSetInfo descriptorSetInfo;
        descriptorSetInfo.descriptorSetCount = 1;
        descriptorSetInfo.pipeline = m_Pipelines.pipeline;

        // First create the descriptor set, but the buffers are empty
        m_DescriptorSets.descriptorSet = new YzVkDescriptorSet();
        m_DescriptorSets.descriptorSet->init(descriptorSetInfo);


        descriptorSetInfo.pipeline = m_Pipelines.skybox;
        m_DescriptorSets.skybox = new YzVkDescriptorSet();
        m_DescriptorSets.skybox->init(descriptorSetInfo);


        std::vector<BufferInfo> bufferInfos = {};
        BufferInfo viewBufferInfo = {};
        viewBufferInfo.buffer = m_UniformBuffers.view->getBuffer();
        viewBufferInfo.offset = 0;
        viewBufferInfo.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        viewBufferInfo.size = sizeof(UniformVS);
        viewBufferInfo.binding = 0;
        viewBufferInfo.imageSampler = nullptr;
        viewBufferInfo.imageView = nullptr;
        viewBufferInfo.descriptorCount = 1;

        BufferInfo dynamicBufferInfo = {};
        dynamicBufferInfo.buffer = m_UniformBuffers.dynamic->getBuffer();
        dynamicBufferInfo.offset = 0;
        dynamicBufferInfo.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        dynamicBufferInfo.size = sizeof(glm::mat4);
        dynamicBufferInfo.binding = 1;
        dynamicBufferInfo.imageSampler = nullptr;
        dynamicBufferInfo.imageView = nullptr;
        dynamicBufferInfo.descriptorCount = 1;

        bufferInfos.push_back(viewBufferInfo);
        bufferInfos.push_back(dynamicBufferInfo);

        BufferInfo imageBufferInfo = {};
        imageBufferInfo.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageBufferInfo.binding = 2;
        imageBufferInfo.descriptorCount = MAX_NUM_TEXTURES;

        int imageIdx = 0;
        for (auto model : m_Models) {
            imageBufferInfo.imageSampler = model->getMaterial()->getTextureImage()->getSampler();
            imageBufferInfo.imageView = model->getMaterial()->getTextureImage()->getImageView();
            bufferInfos.push_back(imageBufferInfo);
            model->setImageIdx(imageIdx++);
        }

        for (size_t i = m_Models.size(); i < MAX_NUM_TEXTURES; i++) {

            imageBufferInfo.imageSampler = m_DefaultMaterial->getTextureImage()->getSampler();
            imageBufferInfo.imageView = m_DefaultMaterial->getTextureImage()->getImageView();
            bufferInfos.push_back(imageBufferInfo);
        }

        m_DescriptorSets.descriptorSet->update(bufferInfos);

        m_SkyboxModel = new Model("../YareZo/Resources/Models/cube.obj");
        m_SkyboxModel->load(MaterialTexType::TextureCube);

        bufferInfos.clear();
        bufferInfos.push_back(viewBufferInfo);

        imageBufferInfo.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageBufferInfo.binding = 1;
        imageBufferInfo.descriptorCount = 1;
        imageBufferInfo.imageSampler = m_SkyboxModel->getMaterial()->getTextureImage()->getSampler();
        imageBufferInfo.imageView = m_SkyboxModel->getMaterial()->getTextureImage()->getImageView();

        bufferInfos.push_back(imageBufferInfo);

        m_DescriptorSets.skybox->update(bufferInfos);
    }

    void ForwardRenderer::createCommandBuffers() {
        m_CommandBuffers.resize(m_FrameBuffers.size());

        for (unsigned int i = 0; i < m_CommandBuffers.size(); i++) {
            m_CurrentBufferID = i;

            m_CommandBuffers[i] = new YzVkCommandBuffer();
            m_CommandBuffers[i]->init();
        }
    }

    void ForwardRenderer::prepareUniformBuffers() {

        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VkDeviceSize viewBufferSize = sizeof(UniformVS);
        VkMemoryPropertyFlags viewPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        m_DynamicAlignment = sizeof(glm::mat4);
        size_t minUboAlignment = YzVkDevice::instance()->getGPUProperties().limits.minUniformBufferOffsetAlignment;
        if (minUboAlignment > 0) {
            m_DynamicAlignment = (m_DynamicAlignment + minUboAlignment - 1) & ~(minUboAlignment - 1);
        }

        VkDeviceSize dynamicBufferSize = MAX_OBJECTS * m_DynamicAlignment;
        m_UboDynamicData.model = (glm::mat4*)alignedAlloc(dynamicBufferSize, m_DynamicAlignment);

        VkMemoryPropertyFlags dynamicPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        m_UniformBuffers.view = new YzVkBuffer(usageFlags, viewPropertyFlags, viewBufferSize, nullptr);
        m_UniformBuffers.dynamic = new YzVkBuffer(usageFlags, dynamicPropertyFlags, dynamicBufferSize, nullptr);


        m_UniformBuffers.skybox = new YzVkBuffer(usageFlags, viewPropertyFlags, viewBufferSize, nullptr);


    }

    void ForwardRenderer::updateUniformBuffers(uint32_t index, const glm::mat4& modelMatrix) {
        // TODO, store UBOs for each model we want to display in one UBO, separated by an offset
        // then bind based on that offset in the present call
        glm::mat4* uboDynamicModelPtr = (glm::mat4*)((uint64_t)m_UboDynamicData.model + (index * m_DynamicAlignment));
        *uboDynamicModelPtr = modelMatrix;

        m_UniformBuffers.dynamic->setDynamicData(MAX_OBJECTS * m_DynamicAlignment, &*m_UboDynamicData.model);

        UniformVS uboVS = {};
        uboVS.view = Application::getAppInstance()->getWindow()->getCamera()->getViewMatrix();
        uboVS.projection = Application::getAppInstance()->getWindow()->getCamera()->getProjectionMatrix();
        uboVS.projection[1][1] *= -1;

        m_UniformBuffers.view->setData(sizeof(uboVS), &uboVS);

        uboVS.view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        m_UniformBuffers.skybox->setData(sizeof(uboVS), &uboVS);


    }

}
