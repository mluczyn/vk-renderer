#include <algorithm>
#include "vkpresent.hpp"

std::vector<const char*> vw::getInstancePresentationExtensions() {
    std::vector<const char*> extensions;
    uint32_t count;
    glfwInit();
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&count);
    extensions.insert(extensions.end(), &glfwExtensions[0], &glfwExtensions[count]);
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    return extensions;
}

vw::Swapchain::Swapchain(vk::Device logicalDevice, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, vk::Queue presentQueue) 
: mDeviceHandle{logicalDevice}, mPresentQueue{presentQueue} {
    //default surface format was checked earlier
    auto deviceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
    mSelectedFormat = { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };
    //FIFO is always supported therefore it is the default
    mSelectedMode = vk::PresentModeKHR::eFifo;

    mSurfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

    vk::SwapchainCreateInfoKHR swapchainCreateInfo;
    swapchainCreateInfo.surface = surface;
    //Triple-buffer if possible
    swapchainCreateInfo.minImageCount = 3 <= mSurfaceCapabilities.maxImageCount ? 3 : mSurfaceCapabilities.maxImageCount;
    swapchainCreateInfo.imageFormat = mSelectedFormat.format;
    swapchainCreateInfo.imageColorSpace = mSelectedFormat.colorSpace;
    swapchainCreateInfo.imageExtent = mSurfaceCapabilities.currentExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapchainCreateInfo.preTransform = mSurfaceCapabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapchainCreateInfo.presentMode = mSelectedMode;
    swapchainCreateInfo.clipped = true;

    mSwapchain = mDeviceHandle.createSwapchainKHR(swapchainCreateInfo);
    mSwapchainImages = mDeviceHandle.getSwapchainImagesKHR(mSwapchain);
    mSwapchainImageViews.reserve(mSwapchainImages.size());

    for (auto& image : mSwapchainImages) {
        mSwapchainImageViews.push_back(mDeviceHandle.createImageView({
            {},
            image,
            vk::ImageViewType::e2D,
            mSelectedFormat.format,
            {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        }));
    }
}

void vw::Swapchain::present(uint32_t imageIndex, ArrayProxy<vk::Semaphore> waitConditions) {
    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitConditions.size());
    presentInfo.pWaitSemaphores = waitConditions.data();
    mPresentQueue.presentKHR(presentInfo);
}

void vw::Swapchain::presentAndSync(uint32_t imageIndex, ArrayProxy<vk::Semaphore> waitConditions) {
    present(imageIndex, waitConditions);
    mPresentQueue.waitIdle();
}

vk::Extent2D vw::Swapchain::getExtent() {
    return mSurfaceCapabilities.currentExtent;
}

vk::Format vw::Swapchain::getImageFormat() {
    return vk::Format::eB8G8R8A8Unorm;
}

vk::Image vw::Swapchain::getImage(uint32_t index) {
    return mSwapchainImages[index];
}

std::vector<vk::ImageView> vw::Swapchain::getImageViews() {
    return mSwapchainImageViews;
}

std::vector<vk::Image> vw::Swapchain::getImages() {
    return mSwapchainImages;
}

uint32_t vw::Swapchain::getNextImageIndex(vk::Semaphore signaledSemaphore) {
    uint32_t imageIndex;
    mDeviceHandle.acquireNextImageKHR(mSwapchain, UINT64_MAX, signaledSemaphore, {}, &imageIndex);
    return imageIndex;
}

vw::Swapchain::~Swapchain() {
    for (auto& imageView : mSwapchainImageViews)
        mDeviceHandle.destroyImageView(imageView);
    mDeviceHandle.destroySwapchainKHR(mSwapchain);
}