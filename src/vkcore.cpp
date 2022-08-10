#include "vkcore.hpp"
#include <algorithm>

namespace vw {
namespace g {
vk::Device device = VK_NULL_HANDLE;
vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
vk::Instance instance = VK_NULL_HANDLE;
}  // namespace g
}  // namespace vw

VkBool32 VKAPI_PTR debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                          VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                          const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                          void* /*pUserData*/) {
  std::cerr << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
            << vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
  std::cerr << "\t"
            << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
  std::cerr << "\t"
            << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
  std::cerr << "\t"
            << "message         = <" << pCallbackData->pMessage << ">\n";
  if (pCallbackData->queueLabelCount) {
    std::cerr << "\t"
              << "Queue Labels:\n";
    for (uint8_t i = 0; i < pCallbackData->queueLabelCount; i++)
      std::cerr << "\t\t"
                << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
  }
  if (pCallbackData->cmdBufLabelCount) {
    std::cerr << "\t"
              << "CommandBuffer Labels:\n";
    for (uint8_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
      std::cerr << "\t\t"
                << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
  }
  if (pCallbackData->objectCount) {
    std::cerr << "\t"
              << "Objects:\n";
    for (uint8_t i = 0; i < pCallbackData->objectCount; i++) {
      std::cerr << "\t\t"
                << "Object " << i << "\n";
      std::cerr << "\t\t\t"
                << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType)) << "\n";
      std::cerr << "\t\t\t"
                << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
      if (pCallbackData->pObjects[i].pObjectName)
        std::cerr << "\t\t\t"
                  << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
    }
  }
  return VK_TRUE;
}

vw::Instance::Instance(std::string appName, uint32_t version, std::vector<const char*> platformExtensions, bool enableValidation) {
  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                                                      vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                     vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
  std::array<vk::ValidationFeatureEnableEXT, 1> enableFeatures{vk::ValidationFeatureEnableEXT::eDebugPrintf};
  vk::ValidationFeaturesEXT validationFeatures{enableFeatures, {}};
  vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {{}, severityFlags, messageTypeFlags, &debugMessengerCallback};
  if (enableValidation) {
    platformExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (!checkValidationLayerSupport())
      throw std::runtime_error("VwInstance: Validation mode not available!");
  }
  if (!checkExtensionSupport(platformExtensions))
    throw std::runtime_error("VwInstance: Extensions not available!");

  vk::ApplicationInfo appInfo;
  appInfo.pApplicationName = appName.c_str();
  appInfo.applicationVersion = version;
  appInfo.pEngineName = appName.append(" Engine").c_str();
  appInfo.engineVersion = version;
  appInfo.apiVersion = VK_API_VERSION_1_2;

  vk::InstanceCreateInfo createInfo;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(platformExtensions.size());
  createInfo.ppEnabledExtensionNames = platformExtensions.data();
  if (enableValidation) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(DebugValidationLayers.size());
    createInfo.ppEnabledLayerNames = DebugValidationLayers.data();
    debugMessengerCreateInfo.pNext = &validationFeatures;
    createInfo.pNext = &debugMessengerCreateInfo;
  }

  vk::Instance::operator=(vk::createInstance(createInfo));
  vw::g::instance = *this;

  if (enableValidation) {
    vk::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    vk::DispatchLoaderDynamic instanceLoader(static_cast<VkInstance>(*this), vkGetInstanceProcAddr);
    mDebugMessenger = vk::Instance::createDebugUtilsMessengerEXT(debugMessengerCreateInfo, nullptr, instanceLoader);
  }

  for (auto& device : enumeratePhysicalDevices())
    mPhysicalDevices.emplace_back(device);
}

vw::Instance::~Instance() {
  if (mDebugMessenger) {
    vk::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    vk::DispatchLoaderDynamic instanceLoader(static_cast<VkInstance>(*this), vkGetInstanceProcAddr);
    vk::Instance::destroyDebugUtilsMessengerEXT(mDebugMessenger, nullptr, instanceLoader);
  }
  vk::Instance::destroy();
}

std::optional<vk::PhysicalDevice> vw::Instance::findPhysicalDevice(ArrayProxy<QueueWorkType> workTypes, ArrayProxy<const std::string> extensions) const {
  auto it = std::find_if(mPhysicalDevices.begin(), mPhysicalDevices.end(), [&](const vw::PhysicalDevice& physicalDevice) {
    if (!physicalDevice.queryExtensionSupport(extensions))
      return false;

    for (auto& workType : workTypes) {
      if (!physicalDevice.queryWorkTypeSupport(workType))
        return false;
    }

    return true;
  });
  return (it == mPhysicalDevices.end()) ? std::optional<vk::PhysicalDevice>{} : std::optional<vk::PhysicalDevice>{*it};
}

bool vw::Instance::checkValidationLayerSupport() {
  size_t requiredLayerCount = vw::DebugValidationLayers.size();
  auto availableLayers = vk::enumerateInstanceLayerProperties();
  for (auto& layerR : vw::DebugValidationLayers) {
    for (auto& layerA : availableLayers) {
      if (std::string(layerR) == layerA.layerName)
        requiredLayerCount--;
    }
  }

  return requiredLayerCount == 0;
}

bool vw::Instance::checkExtensionSupport(std::vector<const char*> extensions) {
  size_t requiredExtensionCount = extensions.size();
  auto availableExtensions = vk::enumerateInstanceExtensionProperties();

  for (auto& extensionR : extensions) {
    for (auto& extensionA : availableExtensions) {
      if (strcmp(extensionR, extensionA.extensionName) == 0)
        requiredExtensionCount--;
    }
  }
  return requiredExtensionCount == 0;
}

vw::Device::Device(vk::PhysicalDevice physicalDevice, ArrayProxy<const std::string> extensions)
    : mPhysicalDeviceHandle{physicalDevice}, mQueueFamilies{physicalDevice.getQueueFamilyProperties()}, mDeviceFeatures{physicalDevice.getFeatures()} {
  // Create 1 queue of each queue family
  uint32_t queueFamilyCount = vw::size32(mQueueFamilies);
  std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
  queueCreateInfos.reserve(queueFamilyCount);

  float queuePriority = 1.0f;
  for (uint32_t i = 0; i < queueFamilyCount; ++i)
    queueCreateInfos.push_back(vk::DeviceQueueCreateInfo{{}, i, 1, &queuePriority});

  std::vector<const char*> rawExtensions;
  rawExtensions.reserve(extensions.size());
  for (auto& extension : extensions)
    rawExtensions.push_back(extension.c_str());

  vk::DeviceCreateInfo createInfo{{}, vw::size32(queueCreateInfos), queueCreateInfos.data(), {},
                                  {}, vw::size32(rawExtensions),    rawExtensions.data(),    &mDeviceFeatures};
  vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures;
  indexingFeatures.descriptorBindingVariableDescriptorCount = true;
  indexingFeatures.runtimeDescriptorArray = true;
  indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
  vk::PhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures;
  shaderDrawParametersFeatures.shaderDrawParameters = true;
  indexingFeatures.pNext = &shaderDrawParametersFeatures;
  createInfo.pNext = &indexingFeatures;
  vk::Device::operator=(physicalDevice.createDevice(createInfo));
  vw::g::physicalDevice = physicalDevice;
  vw::g::device = *this;
  mQueues.reserve(mQueueFamilies.size());
  for (uint32_t i = 0; i < queueFamilyCount; ++i)
    mQueues.emplace_back(getQueue(i, 0), i);
}

vw::Device::~Device() {
  mQueues.clear();
  vk::Device::destroy();
}

uint32_t vw::Device::getPreferredQueueFamily(const vw::QueueWorkType& workType) const {
  std::optional<uint32_t> selectedIndex;
  for (uint32_t i = 0; i < mQueueFamilies.size(); ++i) {
    if (workType.surfaceSupport && !mPhysicalDeviceHandle.getSurfaceSupportKHR(i, workType.surfaceSupport))
      continue;

    auto flags = mQueueFamilies[i].queueFlags;
    if (workType.flags == flags)
      return i;
    else if (!selectedIndex && ((workType.flags & flags) == workType.flags))
      selectedIndex = i;
  }
  if (!selectedIndex)
    throw std::runtime_error("VwDevice: Failed to find compatible queue family!");
  return selectedIndex.value();
}

void vw::Device::waitIdle() {
  for (auto& queue : mQueues)
    queue.waitIdle();
}

void vw::CommandPool::allocateBuffers(uint32_t count) {
  if (count == 0)
    return;

  vk::CommandBufferAllocateInfo allocateInfo{mHandle, vk::CommandBufferLevel::ePrimary, count};
  auto newBuffers = vw::g::device.allocateCommandBuffers(allocateInfo);
  mBuffers.insert(mBuffers.end(), newBuffers.begin(), newBuffers.end());
}

vw::PhysicalDevice::PhysicalDevice(vk::PhysicalDevice physicalDevice)
    : vk::PhysicalDevice{physicalDevice}, mQueueFamilyProperties{physicalDevice.getQueueFamilyProperties()} {
  auto extensions = enumerateDeviceExtensionProperties();
  mSupportedExtensions.reserve(extensions.size());
  for (auto& extensionProps : extensions)
    mSupportedExtensions.push_back(extensionProps.extensionName);
}

bool vw::PhysicalDevice::queryWorkTypeSupport(const QueueWorkType& workType) const {
  for (size_t i = 0; i < mQueueFamilyProperties.size(); ++i) {
    bool flagsSupported = (mQueueFamilyProperties[i].queueFlags & workType.flags) == workType.flags;
    bool surfaceSupported = workType.surfaceSupport ? getSurfaceSupportKHR(static_cast<uint32_t>(i), workType.surfaceSupport) : true;
    if (flagsSupported && surfaceSupported)
      return true;
  }
  return false;
}

bool vw::PhysicalDevice::queryExtensionSupport(ArrayProxy<const std::string> extensions) const {
  for (auto& extension : extensions) {
    if (std::find(mSupportedExtensions.begin(), mSupportedExtensions.end(), extension) == mSupportedExtensions.end())
      return false;
  }
  return true;
}