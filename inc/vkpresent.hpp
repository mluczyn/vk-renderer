#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <iostream>
#include "vkutils.hpp"
#include "vulkan/vulkan.hpp"
#include <GLFW\glfw3.h>

namespace vw {

static const std::string swapchainExtension{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

std::vector<const char*> getInstancePresentationExtensions();

template <typename InputHandlerType>
class Window {
 public:
  Window(vk::Instance instance, vk::Extent2D extent, std::string title) : mInstanceHandle{instance} {
    if (!glfwInit())
      throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, false);
    mWindowHandle = glfwCreateWindow(extent.width, extent.height, title.c_str(), nullptr, nullptr);

    glfwSetInputMode(mWindowHandle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(mWindowHandle, [](GLFWwindow* window, double x, double y) {
      auto inputHandler = reinterpret_cast<InputHandlerType*>(glfwGetWindowUserPointer(window));
      int width, height;
      glfwGetWindowSize(window, &width, &height);
      if (inputHandler)
        inputHandler->onCursorPosChange(x / width, y / height);
    });
    glfwSetKeyCallback(mWindowHandle, [](GLFWwindow* window, int key, int, int action, int mods) {
      auto inputHandler = reinterpret_cast<InputHandlerType*>(glfwGetWindowUserPointer(window));
      if (inputHandler)
        inputHandler->onKeyEvent(key, action, mods);
    });
    glfwCreateWindowSurface(static_cast<VkInstance>(mInstanceHandle), mWindowHandle, nullptr, &mSurfaceHandle);
  }
  ~Window() {
    mInstanceHandle.destroySurfaceKHR(mSurfaceHandle);
    glfwDestroyWindow(mWindowHandle);
  }
  vk::SurfaceKHR getSurface() const {
    return vk::SurfaceKHR{mSurfaceHandle};
  }
  operator GLFWwindow*() const {
    return mWindowHandle;
  }
  inline int untilClosed(std::function<void()> loop) const {
    while (!shouldClose()) {
      auto startTime = std::chrono::high_resolution_clock::now();
      loop();
      auto deltaTime = std::chrono::high_resolution_clock::now() - startTime;
      int64_t microTime = std::chrono::duration_cast<std::chrono::microseconds>(deltaTime).count();
      if (mInputHandler) {
        mInputHandler->setLastDeltaTime(microTime);
      }
      glfwPollEvents();
    }
    return 0;
  }
  inline bool shouldClose() const {
    return glfwWindowShouldClose(mWindowHandle);
  }
  void setInputHandler(InputHandlerType& inputHandler) {
    mInputHandler = &inputHandler;
    glfwSetWindowUserPointer(mWindowHandle, &inputHandler);
  }

 private:
  vk::Instance mInstanceHandle;
  GLFWwindow* mWindowHandle;
  InputHandlerType* mInputHandler = nullptr;
  VkSurfaceKHR mSurfaceHandle;
};

class Swapchain {
 public:
  Swapchain(vk::Device logicalDevice, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, vk::Queue presentQueue);
  void present(uint32_t imageIndex, vw::ArrayProxy<vk::Semaphore> waitConditions);
  void presentAndSync(uint32_t imageIndex, vw::ArrayProxy<vk::Semaphore> waitConditions);
  vk::Extent2D getExtent();
  vk::Format getImageFormat();
  vk::Image getImage(uint32_t index);
  std::vector<vk::ImageView> getImageViews();
  std::vector<vk::Image> getImages();
  uint32_t getNextImageIndex(vk::Semaphore signaledSemaphore);
  ~Swapchain();

 private:
  vk::SwapchainKHR mSwapchain;
  std::vector<vk::Image> mSwapchainImages;
  std::vector<vk::ImageView> mSwapchainImageViews;

  vk::Device mDeviceHandle;
  vk::Queue mPresentQueue;
  vk::SurfaceCapabilitiesKHR mSurfaceCapabilities;
  vk::SurfaceFormatKHR mSelectedFormat;
  vk::PresentModeKHR mSelectedMode;
};
}  // namespace vw