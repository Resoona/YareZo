//
// Created by Drew on 11/6/2019.
//

#include <iostream>
#include <stdexcept>
#include <set>
#include <cstdint>
#include <algorithm>

#include "Utilities/YzLogger.h"
#include "Utilities/IOHelper.h"

#include "Platform/Vulkan/Vk.h"
#include "src/Vulkan.h"
#include "Window.h"
#include "src/Application.h"

namespace Yarezo {


    GraphicsDevice_Vulkan::GraphicsDevice_Vulkan() {
        initVulkan();
    }

    GraphicsDevice_Vulkan::~GraphicsDevice_Vulkan() {
        cleanupSwapChain();
        m_VkPipeline.cleanupDescSetLayout();

        vkDestroyBuffer(m_VkDevice->getDevice(), m_IndexBuffer, nullptr);
        vkFreeMemory(m_VkDevice->getDevice(), m_IndexBufferMemory, nullptr);

        vkDestroyBuffer(m_VkDevice->getDevice(), m_VertexBuffer, nullptr);
        vkFreeMemory(m_VkDevice->getDevice(), m_VertexBufferMemory, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_VkDevice->getDevice(), m_RenderFinishedSemaphore[i], nullptr);
            vkDestroySemaphore(m_VkDevice->getDevice(), m_ImageAvailableSemaphore[i], nullptr);
            vkDestroyFence(m_VkDevice->getDevice(), m_InFlightFences[i], nullptr);
        }

        vkDestroyCommandPool(m_VkDevice->getDevice(), m_CommandPool, nullptr);

        Graphics::YzVkDevice::release();
    }

    void GraphicsDevice_Vulkan::cleanupSwapChain() {

        for (int i = m_VkFramebuffers.size() - 1; i >= 0; i--) {
            m_VkFramebuffers[i].cleanUp();
            m_VkFramebuffers.pop_back();
        }

        vkFreeCommandBuffers(m_VkDevice->getDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());

        m_VkPipeline.cleanUp();
        m_VkRenderPass.cleanUp();
        m_VkSwapchain.cleanUp();

        for (size_t i = 0; i < m_VkSwapchain.getImagesSize(); i++) {
            vkDestroyBuffer(m_VkDevice->getDevice(), m_UniformBuffers[i], nullptr);
            vkFreeMemory(m_VkDevice->getDevice(), m_UniformBuffersMemory[i], nullptr);
        }

        vkDestroyDescriptorPool(m_VkDevice->getDevice(), m_DescriptorPool, nullptr);
    }

    void GraphicsDevice_Vulkan::createGraphicsPipeline() {
        auto vertShaderCode = Utilities::readFile("..\\..\\..\\..\\YareZo\\Shaders\\uboVert.spv");
        auto fragShaderCode = Utilities::readFile("..\\..\\..\\..\\YareZo\\Shaders\\uboFrag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        Graphics::PipelineInfo pipelineInfo2 = { { vertShaderStageInfo, fragShaderStageInfo }, &m_VkRenderPass, &m_VkSwapchain };
        m_VkPipeline.init(pipelineInfo2);

        vkDestroyShaderModule(m_VkDevice->getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_VkDevice->getDevice(), vertShaderModule, nullptr);
    }


    void GraphicsDevice_Vulkan::initVulkan() {

        m_VkInstance.init();

        m_VkDevice = Graphics::YzVkDevice::instance();

        // Create a swapchain, a swapchain is responsible for maintaining the images
        // that will be presented to the user. 
        m_VkSwapchain.init();

        // Create the Renderpass, the render pass is responsible for the draw calls.
        // It creates a description/map of a graphics job.
        Graphics::RenderPassInfo renderPassInfo{ m_VkSwapchain.getImageFormat() };
        m_VkRenderPass.init(renderPassInfo);


        createGraphicsPipeline();


        createFramebuffers();


        // Create a command pool which will manage the memory to store command buffers.
        createCommandPool();
        // Create a vertex buffer, which will store our arbitrary data read by the GPU.
        // Our triangle will be loaded into this buffer to be rendered.
        createVertexBuffer();
        // Create a buffer which will store our vertex indices. This way when we draw triangles
        // We are able to re-use some vertices instead of re-defining them.
        createIndexBuffer();
        // Create the buffers for the projection matrices
        createUniformBuffers();
        // Descriptor sets can't be created directly, they must be allocated from a pool like command buffers. We create those here.
        createDescriptorPool();
        // Create a set of descriptors for the shader to receive,
        // A descriptor set is called a "set" because it can refer to an array of homogenous resources that can be described with the same layout binding. 
        createDescriptorSets();
        // Create a command buffer which will record commands that are submitted for execution on the GPU
        createCommandBuffers();
        // Create some semophores/fences to manage workloads in flight to the gpu
        createSyncObjects();
    }

    void GraphicsDevice_Vulkan::drawFrame() {
        vkWaitForFences(m_VkDevice->getDevice(), 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_VkDevice->getDevice(), m_VkSwapchain.getSwapchain(), UINT64_MAX, m_ImageAvailableSemaphore[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

        auto window = Application::getAppInstance()->getWindow();

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window->windowResized) {
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            YZ_ERROR("Vulkan failed to aquire a swapchain image.");
            throw std::runtime_error("Vulkan failed to acquire swap chain image!");
        }

        updateUniformBuffer(imageIndex);

        if (m_ImagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(m_VkDevice->getDevice(), 1, &m_ImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }

        m_ImagesInFlight[imageIndex] = m_InFlightFences[m_CurrentFrame];

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore[m_CurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore[m_CurrentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(m_VkDevice->getDevice(), 1, &m_InFlightFences[m_CurrentFrame]);
        if (vkQueueSubmit(m_VkDevice->getGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) != VK_SUCCESS) {
            YZ_ERROR("Vulkan failed to submit draw command buffer.");
            throw std::runtime_error("Vulkan failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { m_VkSwapchain.getSwapchain() };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr; // Optional
        result = vkQueuePresentKHR(m_VkDevice->getPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
        }
        else if (result != VK_SUCCESS) {
            YZ_ERROR("Vulkan failed to present a swap chain image.");
            throw std::runtime_error("Vulkan failed to present swap chain image!");
        }

        vkQueueWaitIdle(m_VkDevice->getPresentQueue());

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void GraphicsDevice_Vulkan::waitIdle() {
        m_VkDevice->waitIdle();
    }

    void GraphicsDevice_Vulkan::createFramebuffers() {
        Graphics::FramebufferInfo framebufferInfo;
        framebufferInfo.type = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = &m_VkRenderPass;
        framebufferInfo.width = m_VkSwapchain.getExtent().width;
        framebufferInfo.height = m_VkSwapchain.getExtent().height;
        framebufferInfo.layers = 1;

        for (uint32_t i = 0; i < m_VkSwapchain.getImageViewSize(); i++) {
            framebufferInfo.attachments = { m_VkSwapchain.getImageView(i) };
             m_VkFramebuffers.emplace_back(framebufferInfo);
        }

    }

    void GraphicsDevice_Vulkan::createCommandPool() {
        Graphics::QueueFamilyIndices queueFamilyIndices = m_VkDevice->getQueueFamilyIndicies();

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
        poolInfo.flags = 0; // Optional

        if (vkCreateCommandPool(m_VkDevice->getDevice(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
            YZ_ERROR("Vulkan failed to create a command pool.");
            throw std::runtime_error("Vulkan failed to create a command pool.");
        }
    }

    void GraphicsDevice_Vulkan::createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_VkDevice->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(m_VkDevice->getDevice(), stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_VertexBuffer, m_VertexBufferMemory);

        copyBuffer(stagingBuffer, m_VertexBuffer, bufferSize);

        vkDestroyBuffer(m_VkDevice->getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_VkDevice->getDevice(), stagingBufferMemory, nullptr);

    }

    void GraphicsDevice_Vulkan::createIndexBuffer() {

        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_VkDevice->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(m_VkDevice->getDevice(), stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_IndexBuffer, m_IndexBufferMemory);

        copyBuffer(stagingBuffer, m_IndexBuffer, bufferSize);

        vkDestroyBuffer(m_VkDevice->getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_VkDevice->getDevice(), stagingBufferMemory, nullptr);
    }

    void GraphicsDevice_Vulkan::createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        size_t swapchainImagesSize = m_VkSwapchain.getImagesSize();

        m_UniformBuffers.resize(swapchainImagesSize);
        m_UniformBuffersMemory.resize(swapchainImagesSize);

        for (size_t i = 0; i < swapchainImagesSize; i++) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_UniformBuffers[i], m_UniformBuffersMemory[i]);
        }

    }

    void GraphicsDevice_Vulkan::createDescriptorPool() {
        size_t swapchainImagesSize = m_VkSwapchain.getImagesSize();

        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(swapchainImagesSize);

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(swapchainImagesSize);

        if (vkCreateDescriptorPool(m_VkDevice->getDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            YZ_ERROR("Vulkan creation of descriptor pool failed.");
            throw std::runtime_error("Vulkan creation of descriptor pool failed.");
        }

    }

    void GraphicsDevice_Vulkan::createDescriptorSets() {
        size_t swapchainImagesSize = m_VkSwapchain.getImagesSize();

        std::vector<VkDescriptorSetLayout> layouts(swapchainImagesSize, m_VkPipeline.getDescriptorSetLayout());
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(swapchainImagesSize);
        allocInfo.pSetLayouts = layouts.data();

        m_DescriptorSets.resize(swapchainImagesSize);

        //This wont need to be cleaned up because it is auto cleaned up when the pool is destroyed
        if (vkAllocateDescriptorSets(m_VkDevice->getDevice(), &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
            YZ_ERROR("Vulkan was unable to allocate descriptor sets.");
            throw std::runtime_error("Vulkan was unable to allocate descriptor sets.");
        }

        for (size_t i = 0; i < swapchainImagesSize; i++) {
            VkDescriptorBufferInfo bufferInfo = {};
            bufferInfo.buffer = m_UniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptorWrite = {};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_DescriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr; // Optional
            descriptorWrite.pTexelBufferView = nullptr; // Optional
            vkUpdateDescriptorSets(m_VkDevice->getDevice(), 1, &descriptorWrite, 0, nullptr);
        }
    }

    void GraphicsDevice_Vulkan::createCommandBuffers() {
        m_CommandBuffers.resize(m_VkFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        if (vkAllocateCommandBuffers(m_VkDevice->getDevice(), &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
            YZ_ERROR("Vulkan Failed to allocate command buffers.");
            throw std::runtime_error("Vulkan Failed to allocate command buffers.");
        }

        for (size_t i = 0; i < m_CommandBuffers.size(); i++) {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0; // Optional
            beginInfo.pInheritanceInfo = nullptr; // Optional

            if (vkBeginCommandBuffer(m_CommandBuffers[i], &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("failed to begin recording command buffer!");
            }

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = m_VkRenderPass.getRenderPass();
            renderPassInfo.framebuffer = m_VkFramebuffers[i].getFramebuffer();
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = m_VkSwapchain.getExtent();
            VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(m_CommandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_VkPipeline.getPipeline());

            VkBuffer vertexBuffers[] = { m_VertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, vertexBuffers, offsets);

            vkCmdBindIndexBuffer(m_CommandBuffers[i], m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_VkPipeline.getPipelineLayout(), 0, 1, &m_DescriptorSets[i], 0, nullptr);

            vkCmdDrawIndexed(m_CommandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

            vkCmdEndRenderPass(m_CommandBuffers[i]);

            if (vkEndCommandBuffer(m_CommandBuffers[i]) != VK_SUCCESS) {
                YZ_ERROR("Vulkan failed to record command buffer.");
                throw std::runtime_error("Vulkan failed to record command buffer.");
            }
        }
    }

    void GraphicsDevice_Vulkan::createSyncObjects() {
        m_ImageAvailableSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        m_RenderFinishedSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        m_ImagesInFlight.resize(m_VkSwapchain.getImagesSize(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(m_VkDevice->getDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_VkDevice->getDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore[i]) != VK_SUCCESS ||
                vkCreateFence(m_VkDevice->getDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
                YZ_ERROR("Vulkan failed to create sync objects. (Semaphores or Fence)");
                throw std::runtime_error("Vulkan failed to create sync objects. (Semaphores or Fence)");
            }
        }
    }

    void GraphicsDevice_Vulkan::recreateSwapChain() {
        int width = 0;
        int height = 0;
        GLFWwindow* window = static_cast<GLFWwindow*>(Application::getAppInstance()->getWindow()->getNativeWindow());

        glfwGetFramebufferSize(window, &width, &height);

        if (width == 0 || height == 0) {
            YZ_INFO("Application was minimized.");
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(window, &width, &height);
                glfwWaitEvents();
            }
            YZ_INFO("Application is no longer minimized.");
        }
        YZ_INFO("The application window has been re-sized, the new dimensions [W,H]  are: " + std::to_string(width) + ", " + std::to_string(height));

        m_VkDevice->waitIdle();
        cleanupSwapChain();
        m_VkSwapchain.init();
        Graphics::RenderPassInfo renderPassInfo{ m_VkSwapchain.getImageFormat() };
        m_VkRenderPass.init(renderPassInfo);
        createGraphicsPipeline();
        createFramebuffers();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();

    }

    void GraphicsDevice_Vulkan::updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo = {};
        ubo.model = glm::rotate(glm::mat4(0.5f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        ubo.view = Application::getAppInstance()->getWindow()->getCamera()->getViewMatrix();

        ubo.proj = Application::getAppInstance()->getWindow()->getCamera()->getProjectionMatrix();

        ubo.proj[1][1] *= -1;

        void* data;
        vkMapMemory(m_VkDevice->getDevice(), m_UniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(m_VkDevice->getDevice(), m_UniformBuffersMemory[currentImage]);
    }

    void GraphicsDevice_Vulkan::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_VkDevice->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            YZ_ERROR("Vulkan was unable to create a buffer.");
            throw std::runtime_error("failed to create a buffer");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_VkDevice->getDevice(), buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(m_VkDevice->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            YZ_ERROR("Vulkan failed to allocate buffer memory!");
            throw std::runtime_error("Vulkan failed to allocate buffer memory!");
        }

        vkBindBufferMemory(m_VkDevice->getDevice(), buffer, bufferMemory, 0);
    }

    void GraphicsDevice_Vulkan::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_VkDevice->getDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(m_VkDevice->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_VkDevice->getGraphicsQueue());

        vkFreeCommandBuffers(m_VkDevice->getDevice(), m_CommandPool, 1, &commandBuffer);
    }


    uint32_t GraphicsDevice_Vulkan::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_VkDevice->getGPU(), &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        YZ_ERROR("Vulkan failed to find a suitable memory type.");
        throw std::runtime_error("Vulkan failed to find a suitable memory type.");
    }


    VkSurfaceFormatKHR GraphicsDevice_Vulkan::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR GraphicsDevice_Vulkan::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D GraphicsDevice_Vulkan::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            glfwGetFramebufferSize(static_cast<GLFWwindow*>(Application::getAppInstance()->getWindow()->getNativeWindow()), &width, &height);

            Application::getAppInstance()->getWindow()->getCamera()->updateDimensions((float)width, (float)height);

            VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

            actualExtent.width = std::max(capabilities.minImageExtent.width,
                std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height,
                std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

    VkShaderModule GraphicsDevice_Vulkan::createShaderModule(const std::vector<char>& shader_code) {

        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shader_code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shader_code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_VkDevice->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            YZ_ERROR("Vulkan was unable to create a shaderModule with provided shader code.");
            throw std::runtime_error("Vulkan was unable to create a shaderModule with provided shader code.");
        }

        return shaderModule;
    }
}