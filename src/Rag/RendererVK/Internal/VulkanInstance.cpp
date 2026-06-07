#include "Rag/RendererVK/Internal/VulkanInstance.h"

#include "Rag/Core/Log.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace rag::renderer::vk
{
    namespace
    {
#if defined(RAG_ENABLE_VULKAN_VALIDATION)
        constexpr const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
            void*)
        {
            const char* message = callback_data != nullptr ? callback_data->pMessage : "No validation message.";

            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                RAG_LOG_ERROR("[Vulkan] ", message);
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                RAG_LOG_WARNING("[Vulkan] ", message);
            }
            else
            {
                RAG_LOG_TRACE("[Vulkan] ", message);
            }

            return VK_FALSE;
        }

        VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
        {
            VkDebugUtilsMessengerCreateInfoEXT create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            create_info.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            create_info.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            create_info.pfnUserCallback = DebugCallback;
            return create_info;
        }

        VkResult CreateDebugUtilsMessenger(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT* create_info,
            const VkAllocationCallbacks* allocator,
            VkDebugUtilsMessengerEXT* messenger)
        {
            const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

            if (function == nullptr)
            {
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }

            return function(instance, create_info, allocator, messenger);
        }

        void DestroyDebugUtilsMessenger(
            VkInstance instance,
            VkDebugUtilsMessengerEXT messenger,
            const VkAllocationCallbacks* allocator)
        {
            const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

            if (function != nullptr)
            {
                function(instance, messenger, allocator);
            }
        }
#endif
    }

    VulkanInstance::VulkanInstance(std::string application_name, bool enable_validation)
        : application_name_(std::move(application_name)),
          enable_validation_(enable_validation)
    {
#if !defined(RAG_ENABLE_VULKAN_VALIDATION)
        if (enable_validation_)
        {
            RAG_LOG_WARNING("Vulkan validation was requested, but validation support is not compiled in this build.");
            enable_validation_ = false;
        }
#endif

        if (enable_validation_ && !ValidationLayersSupported())
        {
            throw VulkanError("Vulkan validation was requested, but VK_LAYER_KHRONOS_validation is unavailable.");
        }

        CreateInstance();
        CreateDebugMessenger();
    }

    VulkanInstance::~VulkanInstance()
    {
        DestroyDebugMessenger();

        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    VkInstance VulkanInstance::Get() const
    {
        return instance_;
    }

    bool VulkanInstance::ValidationEnabled() const
    {
        return enable_validation_;
    }

    void VulkanInstance::CreateInstance()
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = application_name_.c_str();
        app_info.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        app_info.pEngineName = "RAG Engine";
        app_info.engineVersion = VK_MAKE_VERSION(0, 2, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        const std::vector<const char*> extensions = RequiredExtensions();

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = static_cast<u32>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

#if defined(RAG_ENABLE_VULKAN_VALIDATION)
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
        const char* validation_layers[] = {ValidationLayer};

        if (enable_validation_)
        {
            debug_create_info = MakeDebugMessengerCreateInfo();
            create_info.enabledLayerCount = 1;
            create_info.ppEnabledLayerNames = validation_layers;
            create_info.pNext = &debug_create_info;
        }
#endif

        RAG_VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance_));
        RAG_LOG_INFO("Created Vulkan instance for RAG Engine.");
    }

    void VulkanInstance::CreateDebugMessenger()
    {
#if defined(RAG_ENABLE_VULKAN_VALIDATION)
        if (!enable_validation_)
        {
            return;
        }

        const VkDebugUtilsMessengerCreateInfoEXT create_info = MakeDebugMessengerCreateInfo();
        RAG_VK_CHECK(CreateDebugUtilsMessenger(instance_, &create_info, nullptr, &debug_messenger_));
        RAG_LOG_INFO("Enabled Vulkan validation layers.");
#endif
    }

    void VulkanInstance::DestroyDebugMessenger()
    {
#if defined(RAG_ENABLE_VULKAN_VALIDATION)
        if (debug_messenger_ != VK_NULL_HANDLE)
        {
            DestroyDebugUtilsMessenger(instance_, debug_messenger_, nullptr);
            debug_messenger_ = VK_NULL_HANDLE;
        }
#endif
    }

    bool VulkanInstance::ValidationLayersSupported() const
    {
#if defined(RAG_ENABLE_VULKAN_VALIDATION)
        u32 layer_count = 0;
        RAG_VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));

        std::vector<VkLayerProperties> layers(layer_count);
        RAG_VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));

        return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
            return std::strcmp(layer.layerName, ValidationLayer) == 0;
        });
#else
        return false;
#endif
    }

    std::vector<const char*> VulkanInstance::RequiredExtensions() const
    {
        std::vector<const char*> extensions;
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

#if defined(RAG_PLATFORM_WINDOWS)
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
        static_assert(false, "Vulkan surface extension selection is not implemented for this platform.");
#endif

        if (enable_validation_)
        {
#if defined(RAG_ENABLE_VULKAN_VALIDATION)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        }

        return extensions;
    }
}
