#ifndef YAREZO_VK_PIPELINE_H
#define YAREZO_VK_PIPELINE_H

#include "Platform/Vulkan/Vk.h"
#include "Platform/Vulkan/Vk_Renderpass.h"
#include "Platform/Vulkan/Vk_Swapchain.h"

#include <glm.hpp>
#include <array>
#include <gtc/matrix_transform.hpp>

namespace Yarezo {
    namespace Graphics {

        struct Vertex {
            glm::vec2 pos;
            glm::vec3 color;

            static VkVertexInputBindingDescription getBindingDescription() {
                VkVertexInputBindingDescription bindingDescription = {};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(Vertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                return bindingDescription;
            }

            static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
                std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {};
                attributeDescriptions[0].binding = 0;
                attributeDescriptions[0].location = 0;
                attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
                attributeDescriptions[0].offset = offsetof(Vertex, pos);

                attributeDescriptions[1].binding = 0;
                attributeDescriptions[1].location = 1;
                attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[1].offset = offsetof(Vertex, color);

                return attributeDescriptions;
            }
        };

        struct PipelineInfo {
            VkPipelineShaderStageCreateInfo shaders[2];
            YzVkRenderPass* renderpass;
            YzVkSwapchain* swapchain;

        };

        class YzVkPipeline {

        public:
            YzVkPipeline();
            ~YzVkPipeline();
            void init(PipelineInfo& pipelineInfo);
            void cleanUp();
            void cleanupDescSetLayout();

            inline VkDescriptorSetLayout getDescriptorSetLayout()  const { return m_DescriptorSetLayout; }
            inline VkPipelineLayout getPipelineLayout()            const { return m_PipelineLayout; }
            inline VkPipeline getPipeline()                        const { return m_GraphicsPipeline; }

        private:
            void createDescriptorSetLayout();
            void createGraphicsPipeline(PipelineInfo& pipelineInfo);

            VkDescriptorSetLayout m_DescriptorSetLayout;
            VkPipelineLayout m_PipelineLayout;
            VkPipeline m_GraphicsPipeline;

        };

    }
}

#endif //YAREZO_VK_PIPELINE_H