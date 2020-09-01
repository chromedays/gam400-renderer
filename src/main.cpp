#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include "external/SDL2/SDL_main.h"
#include "external/SDL2/SDL_syswm.h"
#include "external/SDL2/SDL_vulkan.h"
#include "external/fmt/format.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include <algorithm>
#include <stdint.h>
#include <vector>
#include <array>
#include <optional>
#include <stdio.h>
#include <assert.h>

#if BB_DEBUG
#define BB_ASSERT(exp) assert(exp)
#define BB_LOG_INFO(...) bb::log(bb::LogLevel::Info, __VA_ARGS__)
#define BB_LOG_WARNING(...) bb::log(bb::LogLevel::Warning, __VA_ARGS__)
#define BB_LOG_ERROR(...) bb::log(bb::LogLevel::Error, __VA_ARGS__)
#else
#define BB_ASSERT(exp)
#define BB_LOG_INFO(...)
#define BB_LOG_WARNING(...)
#define BB_LOG_ERROR(...)
#endif
#define BB_VK_ASSERT(exp)                                                      \
  do {                                                                         \
    auto result = exp;                                                         \
    BB_ASSERT(result == VK_SUCCESS);                                           \
  } while (0)

namespace bb {

enum class LogLevel { Info, Warning, Error };

template <typename... Args> void print(Args... args) {
  std::string formatted = fmt::format(args...);
  formatted += "\n";
  OutputDebugStringA(formatted.c_str());
}

template <typename... Args> void log(LogLevel level, Args... args) {
  switch (level) {
  case LogLevel::Info:
    OutputDebugStringA("[Info]:    ");
    break;
  case LogLevel::Warning:
    OutputDebugStringA("[Warning]: ");
    break;
  case LogLevel::Error:
    OutputDebugStringA("[Error]:   ");
    break;
  }

  print(args...);
}

template <typename E, typename T> struct EnumArray {
  T Elems[(int)(E::COUNT)] = {};

  const T &operator[](E _e) const { return Elems[(int)_e]; }

  T &operator[](E _e) { return Elems[(int)_e]; }

  auto begin() { return Elems.begin(); }

  auto end() { return Elems.end(); }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

constexpr float pi32 = 3.141592f;

struct Int2 {
  int X = 0;
  int Y = 0;
};

struct Float2 {
  float X = 0.f;
  float Y = 0.f;
};

struct Float3 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;

  float lengthSq() {
    float result = X * X + Y * Y + Z * Z;
    return result;
  }

  float length() {
    float result = sqrtf(lengthSq());
    return result;
  }

  Float3 normalize() {
    float len = length();
    Float3 result = *this / len;
    return result;
  }

  Float3 operator-(const Float3 &_other) const {
    Float3 result = {X - _other.X, Y - _other.Y, Z - _other.Z};
    return result;
  }

  Float3 operator/(float _divider) const {
    Float3 result = {X / _divider, Y / _divider, Z / _divider};
    return result;
  }
};

inline float dot(const Float3 &_a, const Float3 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z;
}

inline Float3 cross(const Float3 &_a, const Float3 &_b) {
  Float3 result = {
      _a.Y * _b.Z - _a.Z * _b.Y,
      _a.Z * _b.X - _a.X * _b.Z,
      _a.X * _b.Y - _a.Y * _b.X,
  };
  return result;
}

struct Float4 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;
  float W = 0.f;
};

inline float dot(const Float4 &_a, const Float4 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z + _a.W * _b.W;
}

struct Mat4 {
  float M[4][4] = {};

  Float4 row(int _n) const {
    BB_ASSERT(_n >= 0 && _n < 4);
    return {M[0][_n], M[1][_n], M[2][_n], M[3][_n]};
  }

  Float4 column(int _n) const {
    BB_ASSERT(_n >= 0 && _n < 4);
    return {M[_n][0], M[_n][1], M[_n][2], M[_n][3]};
  }

  static Mat4 identity() {
    return {{
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    }};
  }

  static Mat4 lookAt(const Float3 &_eye, const Float3 &_target,
                     const Float3 &_upAxis = {0, 1, 0}) {
    Float3 forward = (_eye - _target).normalize();
    Float3 right = cross(_upAxis, forward).normalize();
    Float3 up = cross(forward, right).normalize();

    // clang-format off
    return {{
      {right.X, up.X, forward.X, 0},
      {right.Y, up.Y, forward.Y, 0},
      {right.Z, up.Z, forward.Z, 0},
      {-dot(_eye, right), -dot(_eye, up), -dot(_eye, forward), 1},
    }};
    // clang-format on
  }

// TODO(ilgwon): Need to be adjusted
#if 1
  static Mat4 ortho(float _left, float _right, float _top, float _bottom,
                    float _nearZ, float _farZ) {
#define NDC_MIN_Z 0
#define NDC_MAX_Z 1
    // clang-format off
    Mat4 result = {{
        {2.f / (_right - _left), 0, 0, 0},
        {0, 2.f / (_top - _bottom), 0, 0},
        {0, 0, (NDC_MAX_Z - NDC_MIN_Z) / (_farZ - _nearZ), 0},
        {(_left + _right) / (_left - _right), (_bottom + _top) / (_bottom - _top),
         _farZ * (NDC_MAX_Z - NDC_MIN_Z) / (_farZ - _nearZ), 1},
    }};
    // clang-format on
    return result;
  }

  static Mat4 orthoCenter(float _width, float _height, float _nearZ,
                          float _farZ) {
    float halfWidth = _width * 0.5f;
    float halfHeight = _height * 0.5f;
    Mat4 result =
        ortho(-halfWidth, halfWidth, halfHeight, -halfHeight, _nearZ, _farZ);
    return result;
  }
#endif
};

inline Mat4 operator*(const Mat4 &_a, const Mat4 &_b) {
  Float4 rows[4] = {_a.row(0), _a.row(1), _a.row(2), _a.row(3)};
  Float4 columns[4] = {_b.column(0), _b.column(1), _b.column(2), _b.column(3)};
  Mat4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.M[i][j] = dot(rows[i], columns[j]);
    }
  }
  return result;
}

struct Vertex {
  Float2 Pos;
  Float3 Color;

  static VkVertexInputBindingDescription getBindingDesc() {
    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDesc;
  }

  static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescs() {
    std::array<VkVertexInputAttributeDescription, 2> attributeDescs = {};
    attributeDescs[0].binding = 0;
    attributeDescs[0].location = 0;
    attributeDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[0].offset = offsetof(Vertex, Pos);
    attributeDescs[1].binding = 0;
    attributeDescs[1].location = 1;
    attributeDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[1].offset = offsetof(Vertex, Color);

    return attributeDescs;
  }
};

VKAPI_ATTR VkBool32 VKAPI_CALL
vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT _severity,
                    VkDebugUtilsMessageTypeFlagsEXT _type,
                    const VkDebugUtilsMessengerCallbackDataEXT *_callbackData,
                    void *_userData) {
  printf("%s\n", _callbackData->pMessage);
  switch (_severity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    BB_LOG_INFO("Vulkan validation: {}", _callbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    BB_LOG_WARNING("Vulkan validation: {}", _callbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    BB_LOG_ERROR("Vulkan validation: {}", _callbackData->pMessage);
    break;
  default:
    break;
  }

  return VK_FALSE;
}

struct QueueFamilyIndices {
  std::optional<uint32_t> Graphics;
  std::optional<uint32_t> Transfer0;
  std::optional<uint32_t> Present;

  bool isCompleted() {
    return Graphics.has_value() && Transfer0.has_value() && Present.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR Capabilities;
  std::vector<VkSurfaceFormatKHR> Formats;
  std::vector<VkPresentModeKHR> PresentModes;

  VkSurfaceFormatKHR chooseSurfaceFormat() const {
    for (const VkSurfaceFormatKHR &format : Formats) {
      if (format.format == VK_FORMAT_R8G8B8A8_SRGB &&
          format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }

    return Formats[0];
  }

  VkPresentModeKHR choosePresentMode() const {
    for (VkPresentModeKHR presentMode : PresentModes) {
      if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return presentMode;
      }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D chooseExtent(uint32_t width, uint32_t height) const {
    if (Capabilities.currentExtent.width != UINT32_MAX) {
      return Capabilities.currentExtent;
    } else {
      VkExtent2D extent;
      extent.width = std::clamp(width, Capabilities.minImageExtent.width,
                                Capabilities.maxImageExtent.width);
      extent.height = std::clamp(height, Capabilities.minImageExtent.height,
                                 Capabilities.maxImageExtent.height);
      return extent;
    }
  }
};

QueueFamilyIndices getQueueFamily(VkPhysicalDevice _physicalDevice,
                                  VkSurfaceKHR _surface) {
  QueueFamilyIndices result = {};

  uint32_t numQueueFamilyProperties = 0;
  std::vector<VkQueueFamilyProperties> queueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice,
                                           &numQueueFamilyProperties, nullptr);
  queueFamilyProperties.resize(numQueueFamilyProperties);
  vkGetPhysicalDeviceQueueFamilyProperties(
      _physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  for (uint32_t i = 0; i < numQueueFamilyProperties; i++) {
    // Make queues exclusive per task.
    if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        !result.Graphics.has_value()) {
      result.Graphics = i;
    } else if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
               !result.Transfer0.has_value()) {
      result.Transfer0 = i;
    } else {
      VkBool32 supportPresent = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, i, _surface,
                                           &supportPresent);
      if (supportPresent) {
        result.Present = i;
      }
    }

    if (result.isCompleted())
      break;
  }

  return result;
}

bool checkPhysicalDevice(VkPhysicalDevice _physicalDevice,
                         VkSurfaceKHR _surface,
                         const std::vector<const char *> &_deviceExtensions,
                         VkPhysicalDeviceFeatures *_outDeviceFeatures,
                         QueueFamilyIndices *_outQueueFamilyIndices,
                         SwapChainSupportDetails *_outSwapChainSupportDetails) {
  VkPhysicalDeviceProperties deviceProperties = {};
  VkPhysicalDeviceFeatures deviceFeatures = {};

  vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
  vkGetPhysicalDeviceFeatures(_physicalDevice, &deviceFeatures);

  QueueFamilyIndices queueFamilyIndices =
      getQueueFamily(_physicalDevice, _surface);

  // Check if all required extensions are supported
  uint32_t numExtensions;
  std::vector<VkExtensionProperties> extensionProperties;
  vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numExtensions,
                                       nullptr);
  extensionProperties.resize(numExtensions);
  vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numExtensions,
                                       extensionProperties.data());
  bool areAllExtensionsSupported = true;
  for (const char *extensionName : _deviceExtensions) {
    bool found = false;

    for (const VkExtensionProperties &properties : extensionProperties) {
      if (strcmp(extensionName, properties.extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      areAllExtensionsSupported = false;
      break;
    }
  }

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface,
                                            &surfaceCapabilities);
  uint32_t numSurfaceFormats;
  std::vector<VkSurfaceFormatKHR> surfaceFormats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface,
                                       &numSurfaceFormats, nullptr);
  surfaceFormats.resize(numSurfaceFormats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(
      _physicalDevice, _surface, &numSurfaceFormats, surfaceFormats.data());

  uint32_t numSurfacePresentModes;
  std::vector<VkPresentModeKHR> surfacePresentModes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface,
                                            &numSurfacePresentModes, nullptr);
  surfacePresentModes.resize(numSurfacePresentModes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface,
                                            &numSurfacePresentModes,
                                            surfacePresentModes.data());
  bool isSwapChainAdequate =
      (numSurfaceFormats > 0) && (numSurfacePresentModes > 0);
  if (_outSwapChainSupportDetails) {
    *_outSwapChainSupportDetails = {};
    _outSwapChainSupportDetails->Capabilities = surfaceCapabilities;
    _outSwapChainSupportDetails->Formats = std::move(surfaceFormats);
    _outSwapChainSupportDetails->PresentModes = std::move(surfacePresentModes);
  }

  bool isProperType =
      (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
      (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
  bool isFeatureComplete =
      deviceFeatures.geometryShader && deviceFeatures.tessellationShader &&
      deviceFeatures.fillModeNonSolid && deviceFeatures.depthClamp;
  bool isQueueComplete = queueFamilyIndices.isCompleted();

  if (_outDeviceFeatures) {
    *_outDeviceFeatures = deviceFeatures;
  }
  if (_outQueueFamilyIndices) {
    *_outQueueFamilyIndices = queueFamilyIndices;
  }

  return areAllExtensionsSupported && isSwapChainAdequate && isProperType &&
         isFeatureComplete && isQueueComplete;
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void updateSwapChain(SDL_Window *_window, VkDevice _device,
                     std::vector<VkCommandBuffer> &_graphicsCmdBuffers,
                     VkCommandPool _graphicsCmdPool,
                     std::vector<VkFramebuffer> &_swapChainFramebuffers,
                     VkPipeline &_graphicsPipeline, VkRenderPass &_renderPass,
                     VkSwapchainKHR &_swapChain,
                     const VkFormat &_swapChainImageFormat,
                     std::vector<VkImage> &_swapChainImages,
                     VkSwapchainCreateInfoKHR &_swapChainCreateInfo,
                     const SwapChainSupportDetails &_swapChainSupportDetails,
                     const VkRenderPassCreateInfo &_renderPassCreateInfo,
                     VkGraphicsPipelineCreateInfo &_graphicsPipelineCreateInfo,
                     uint32_t &_numSwapChainImages,
                     const VkExtent2D &_swapChainExtent,
                     std::vector<VkImageView> &_swapChainImageViews,
                     std::vector<VkFramebuffer> &_outSwapChainFramebuffers,
                     const VkCommandPoolCreateInfo &_cmdPoolCreateInfo) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(_device); // Ensure that device finished using swap chain.

  vkFreeCommandBuffers(_device, _graphicsCmdPool,
                       (uint32_t)_graphicsCmdBuffers.size(),
                       _graphicsCmdBuffers.data());
  vkDestroyCommandPool(_device, _graphicsCmdPool, nullptr);

  for (VkFramebuffer fb : _swapChainFramebuffers) {
    vkDestroyFramebuffer(_device, fb, nullptr);
  }
  _swapChainFramebuffers.clear();

  vkDestroyPipeline(_device, _graphicsPipeline, nullptr);
  vkDestroyRenderPass(_device, _renderPass, nullptr);
  for (VkImageView imageView : _swapChainImageViews) {
    vkDestroyImageView(_device, imageView, nullptr);
  }
  _swapChainImageViews.clear();
  vkDestroySwapchainKHR(_device, _swapChain, nullptr);
  _swapChain = nullptr;
  // Destroy any other buffers used by queues here, including uniform buffers.

  // recreate
  BB_VK_ASSERT(vkCreateSwapchainKHR(_device, &_swapChainCreateInfo, nullptr,
                                    &_swapChain));

  vkGetSwapchainImagesKHR(_device, _swapChain, &_numSwapChainImages, nullptr);
  _swapChainImages.resize(_numSwapChainImages);
  vkGetSwapchainImagesKHR(_device, _swapChain, &_numSwapChainImages,
                          _swapChainImages.data());
  _swapChainImageViews.resize(_numSwapChainImages);
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkImageViewCreateInfo swapChainImageViewCreateInfo = {};
    swapChainImageViewCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    swapChainImageViewCreateInfo.image = _swapChainImages[i];
    swapChainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    swapChainImageViewCreateInfo.format = _swapChainImageFormat;
    swapChainImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    swapChainImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    swapChainImageViewCreateInfo.subresourceRange.levelCount = 1;
    swapChainImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    swapChainImageViewCreateInfo.subresourceRange.layerCount = 1;
    BB_VK_ASSERT(vkCreateImageView(_device, &swapChainImageViewCreateInfo,
                                   nullptr, &_swapChainImageViews[i]));
  }

  BB_VK_ASSERT(vkCreateRenderPass(_device, &_renderPassCreateInfo, nullptr,
                                  &_renderPass));
  _graphicsPipelineCreateInfo.renderPass = _renderPass;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
                                         &_graphicsPipelineCreateInfo, nullptr,
                                         &_graphicsPipeline));

  _outSwapChainFramebuffers.resize(_numSwapChainImages);
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = _renderPass;
    fbCreateInfo.attachmentCount = 1;
    fbCreateInfo.pAttachments = &_swapChainImageViews[i];
    fbCreateInfo.width = _swapChainExtent.width;
    fbCreateInfo.height = _swapChainExtent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_device, &fbCreateInfo, nullptr,
                                     &_outSwapChainFramebuffers[i]));
  }

  BB_VK_ASSERT(vkCreateCommandPool(_device, &_cmdPoolCreateInfo, nullptr,
                                   &_graphicsCmdPool));

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.commandPool = _graphicsCmdPool;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = (uint32_t)_graphicsCmdBuffers.size();
  BB_VK_ASSERT(vkAllocateCommandBuffers(_device, &cmdBufferAllocInfo,
                                        _graphicsCmdBuffers.data()));

  // TODO: this is insane, I need to seperate some of these functions.
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkCommandBuffer cmdBuffer = _graphicsCmdBuffers[i];

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = 0;
    cmdBeginInfo.pInheritanceInfo = nullptr;

    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = _outSwapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapChainExtent;

    VkClearValue clearColor = {0.f, 0.f, 0.f, 1.f};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _graphicsPipeline);
    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmdBuffer);

    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
  }
}

VkShaderModule createShaderModuleFromFile(VkDevice _device,
                                          const std::string &_filePath) {
  FILE *f = fopen(_filePath.c_str(), "rb");
  BB_ASSERT(f);
  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  rewind(f);
  uint8_t *contents = new uint8_t[fileSize];
  fread(contents, sizeof(*contents), fileSize, f);

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = fileSize;
  createInfo.pCode = (uint32_t *)contents;
  VkShaderModule shaderModule;
  BB_VK_ASSERT(
      vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule));

  delete[] contents;
  fclose(f);

  return shaderModule;
}

uint32_t findMemoryType(VkPhysicalDevice _physicalDevice, uint32_t _typeFilter,
                        VkMemoryPropertyFlags _properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if ((_typeFilter & (1 << i)) &&
        ((memProperties.memoryTypes[i].propertyFlags & _properties) ==
         _properties)) {
      return i;
    }
  }

  BB_ASSERT(false);
  return 0;
}

template <typename T> uint32_t size_bytes32(const T &_container) {
  return (uint32_t)(sizeof(typename T::value_type) * _container.size());
}

} // namespace bb

int main(int _argc, char **_argv) {
  using namespace bb;

  BB_VK_ASSERT(volkInitialize());

  SDL_Init(SDL_INIT_VIDEO);
  int width = 1280;
  int height = 720;
  SDL_Window *window = SDL_CreateWindow(
      "Bibim Renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_VULKAN);

  SDL_SysWMinfo sysinfo = {};
  SDL_VERSION(&sysinfo.version);
  SDL_GetWindowWMInfo(window, &sysinfo);

  VkInstance instance;
  VkApplicationInfo appinfo = {};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "Bibim Renderer";
  appinfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appinfo.pEngineName = "Bibim Renderer";
  appinfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appinfo.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo instanceCreateInfo = {};
  instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCreateInfo.pApplicationInfo = &appinfo;

  std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

  bool enableValidationLayers =
#ifdef BB_DEBUG
      true;
#else
      false;
#endif

  uint32_t numLayers;
  vkEnumerateInstanceLayerProperties(&numLayers, nullptr);
  std::vector<VkLayerProperties> layerProperties(numLayers);
  vkEnumerateInstanceLayerProperties(&numLayers, layerProperties.data());
  bool canEnableLayers = true;
  for (const char *layerName : validationLayers) {
    bool foundLayer = false;
    for (VkLayerProperties &properties : layerProperties) {
      if (strcmp(properties.layerName, layerName) == 0) {
        foundLayer = true;
        break;
      }
    }

    if (!foundLayer) {
      canEnableLayers = false;
      break;
    }
  }

  if (enableValidationLayers && canEnableLayers) {
    instanceCreateInfo.enabledLayerCount = (uint32_t)validationLayers.size();
    instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();
  }

  std::vector<const char *> extensions;
  // TODO: ADD vk_win32_surface extension.
  unsigned numInstantExtensions = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &numInstantExtensions, nullptr);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  unsigned numExtraInstantExtensions = extensions.size();
  extensions.resize(numExtraInstantExtensions + numInstantExtensions);

  SDL_Vulkan_GetInstanceExtensions(window, &numInstantExtensions,
                                   extensions.data() +
                                       numExtraInstantExtensions);

  instanceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
  instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

  BB_VK_ASSERT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
  volkLoadInstance(instance);

  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  if (enableValidationLayers) {
    VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {};
    messengerCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerCreateInfo.pfnUserCallback = &vulkanDebugCallback;
    BB_VK_ASSERT(vkCreateDebugUtilsMessengerEXT(instance, &messengerCreateInfo,
                                                nullptr, &messenger));
  }

  VkSurfaceKHR surface = {};
  BB_VK_ASSERT(!SDL_Vulkan_CreateSurface(
      window, instance, &surface)); // ! to convert SDL_bool to VkResult

  uint32_t numPhysicalDevices = 0;
  std::vector<VkPhysicalDevice> physicalDevices;
  BB_VK_ASSERT(
      vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr));
  physicalDevices.resize(numPhysicalDevices);
  BB_VK_ASSERT(vkEnumeratePhysicalDevices(instance, &numPhysicalDevices,
                                          physicalDevices.data()));

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceFeatures deviceFeatures = {};
  QueueFamilyIndices queueFamilyIndices = {};
  SwapChainSupportDetails swapChainSupportDetails = {};

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  for (VkPhysicalDevice currentPhysicalDevice : physicalDevices) {
    if (checkPhysicalDevice(currentPhysicalDevice, surface, deviceExtensions,
                            &deviceFeatures, &queueFamilyIndices,
                            &swapChainSupportDetails)) {
      physicalDevice = currentPhysicalDevice;
      break;
    }
  }
  BB_ASSERT(physicalDevice != VK_NULL_HANDLE);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

  queueCreateInfos.resize(3);
  float queuePriority = 1.f;
  queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfos[0].queueFamilyIndex = queueFamilyIndices.Graphics.value();
  queueCreateInfos[0].queueCount = 1;
  queueCreateInfos[0].pQueuePriorities = &queuePriority;
  queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfos[1].queueFamilyIndex = queueFamilyIndices.Transfer0.value();
  queueCreateInfos[1].queueCount = 1;
  queueCreateInfos[1].pQueuePriorities = &queuePriority;
  queueCreateInfos[2].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfos[2].queueFamilyIndex = queueFamilyIndices.Present.value();
  queueCreateInfos[2].queueCount = 1;
  queueCreateInfos[2].pQueuePriorities = &queuePriority;

  VkDeviceCreateInfo deviceCreateInfo = {};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
  deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
  deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
  deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
  VkDevice device;
  BB_VK_ASSERT(
      vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
  VkQueue graphicsQueue;
  VkQueue transferQueue;
  VkQueue presentQueue;
  vkGetDeviceQueue(device, queueFamilyIndices.Graphics.value(), 0,
                   &graphicsQueue);
  vkGetDeviceQueue(device, queueFamilyIndices.Transfer0.value(), 0,
                   &transferQueue);
  vkGetDeviceQueue(device, queueFamilyIndices.Present.value(), 0,
                   &presentQueue);
  BB_ASSERT(graphicsQueue != VK_NULL_HANDLE &&
            transferQueue != VK_NULL_HANDLE && presentQueue != VK_NULL_HANDLE);

  VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
  swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapChainCreateInfo.surface = surface;
  swapChainCreateInfo.minImageCount =
      swapChainSupportDetails.Capabilities.minImageCount + 1;
  if (swapChainSupportDetails.Capabilities.maxImageCount > 0 &&
      swapChainCreateInfo.minImageCount >
          swapChainSupportDetails.Capabilities.maxImageCount) {
    swapChainCreateInfo.minImageCount =
        swapChainSupportDetails.Capabilities.maxImageCount;
  }
  VkSurfaceFormatKHR surfaceFormat =
      swapChainSupportDetails.chooseSurfaceFormat();
  swapChainCreateInfo.imageFormat = surfaceFormat.format;
  swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
  swapChainCreateInfo.imageExtent =
      swapChainSupportDetails.chooseExtent(width, height);
  swapChainCreateInfo.imageArrayLayers = 1;
  swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t sharedQueueFamilyIndices[2] = {queueFamilyIndices.Graphics.value(),
                                          queueFamilyIndices.Present.value()};
  if (queueFamilyIndices.Graphics != queueFamilyIndices.Present) {
    // TODO(ilgwon): Can be an interesting optimization to use EXCLUSIVE mode
    // with multiple queues
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapChainCreateInfo.queueFamilyIndexCount = 2;
    swapChainCreateInfo.pQueueFamilyIndices = sharedQueueFamilyIndices;
  } else {
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  swapChainCreateInfo.preTransform =
      swapChainSupportDetails.Capabilities.currentTransform;
  swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapChainCreateInfo.presentMode = swapChainSupportDetails.choosePresentMode();
  swapChainCreateInfo.clipped = VK_TRUE;
  swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

  VkSwapchainKHR swapChain;
  VkFormat swapChainImageFormat = swapChainCreateInfo.imageFormat;
  VkExtent2D swapChainExtent = swapChainCreateInfo.imageExtent;
  BB_VK_ASSERT(
      vkCreateSwapchainKHR(device, &swapChainCreateInfo, nullptr, &swapChain));

  uint32_t numSwapChainImages;
  std::vector<VkImage> swapChainImages;
  vkGetSwapchainImagesKHR(device, swapChain, &numSwapChainImages, nullptr);
  swapChainImages.resize(numSwapChainImages);
  vkGetSwapchainImagesKHR(device, swapChain, &numSwapChainImages,
                          swapChainImages.data());

  std::vector<VkImageView> swapChainImageViews(numSwapChainImages);
  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    VkImageViewCreateInfo swapChainImageViewCreateInfo = {};
    swapChainImageViewCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    swapChainImageViewCreateInfo.image = swapChainImages[i];
    swapChainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    swapChainImageViewCreateInfo.format = swapChainImageFormat;
    swapChainImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    swapChainImageViewCreateInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    swapChainImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    swapChainImageViewCreateInfo.subresourceRange.levelCount = 1;
    swapChainImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    swapChainImageViewCreateInfo.subresourceRange.layerCount = 1;
    BB_VK_ASSERT(vkCreateImageView(device, &swapChainImageViewCreateInfo,
                                   nullptr, &swapChainImageViews[i]));
  }

  std::string resourceRootPath = SDL_GetBasePath();
  resourceRootPath += "\\..\\..\\resources\\";

  VkShaderModule testVertShaderModule = createShaderModuleFromFile(
      device, resourceRootPath + "..\\src\\shaders\\test.vert.spv");
  VkShaderModule testFragShaderModule = createShaderModuleFromFile(
      device, resourceRootPath + "..\\src\\shaders\\test.frag.spv");

  VkPipelineShaderStageCreateInfo shaderStages[2] = {};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = testVertShaderModule;
  shaderStages[0].pName = "main";
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = testFragShaderModule;
  shaderStages[1].pName = "main";

  VkVertexInputBindingDescription bindingDesc = Vertex::getBindingDesc();
  std::array<VkVertexInputAttributeDescription, 2> attributeDescs =
      Vertex::getAttributeDescs();
  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount = 1;
  vertexInputState.pVertexBindingDescriptions = &bindingDesc;
  vertexInputState.vertexAttributeDescriptionCount =
      (uint32_t)attributeDescs.size();
  vertexInputState.pVertexAttributeDescriptions = attributeDescs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
  inputAssemblyState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssemblyState.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.width = (float)swapChainExtent.width;
  viewport.height = (float)swapChainExtent.height;
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizationState = {};
  rasterizationState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.depthClampEnable = VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.lineWidth = 1.f;
  rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.depthBiasConstantFactor = 0.f;
  rasterizationState.depthBiasClamp = 0.f;
  rasterizationState.depthBiasSlopeFactor = 0.f;

  VkPipelineMultisampleStateCreateInfo multisampleState = {};
  multisampleState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.sampleShadingEnable = VK_FALSE;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampleState.minSampleShading = 1.f;
  multisampleState.pSampleMask = nullptr;
  multisampleState.alphaToCoverageEnable = VK_FALSE;
  multisampleState.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
  colorBlendAttachmentState.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachmentState.blendEnable = VK_FALSE;
  colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlendState = {};
  colorBlendState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.logicOpEnable = VK_FALSE;
  colorBlendState.logicOp = VK_LOGIC_OP_COPY;
  colorBlendState.attachmentCount = 1;
  colorBlendState.pAttachments = &colorBlendAttachmentState;
  colorBlendState.blendConstants[0] = 0.f;
  colorBlendState.blendConstants[1] = 0.f;
  colorBlendState.blendConstants[2] = 0.f;
  colorBlendState.blendConstants[3] = 0.f;

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCreateInfo.setLayoutCount = 0;
  pipelineLayoutCreateInfo.pSetLayouts = nullptr;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout pipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo,
                                      nullptr, &pipelineLayout));

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = swapChainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency subpassDependency = {};
  subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpassDependency.dstSubpass = 0;
  subpassDependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.srcAccessMask = 0;
  subpassDependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassCreateInfo = {};
  renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCreateInfo.attachmentCount = 1;
  renderPassCreateInfo.pAttachments = &colorAttachment;
  renderPassCreateInfo.subpassCount = 1;
  renderPassCreateInfo.pSubpasses = &subpass;
  renderPassCreateInfo.dependencyCount = 1;
  renderPassCreateInfo.pDependencies = &subpassDependency;

  VkRenderPass renderPass;
  BB_VK_ASSERT(
      vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass));

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.stageCount = 2;
  pipelineCreateInfo.pStages = shaderStages;
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = nullptr;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;
  pipelineCreateInfo.pDynamicState = nullptr;
  pipelineCreateInfo.layout = pipelineLayout;
  pipelineCreateInfo.renderPass = renderPass;
  pipelineCreateInfo.subpass = 0;
  pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCreateInfo.basePipelineIndex = -1;

  VkPipeline graphicsPipeline;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                         &pipelineCreateInfo, nullptr,
                                         &graphicsPipeline));

  std::vector<VkFramebuffer> swapChainFramebuffers(numSwapChainImages);
  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = renderPass;
    fbCreateInfo.attachmentCount = 1;
    fbCreateInfo.pAttachments = &swapChainImageViews[i];
    fbCreateInfo.width = swapChainExtent.width;
    fbCreateInfo.height = swapChainExtent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(device, &fbCreateInfo, nullptr,
                                     &swapChainFramebuffers[i]));
  }

  std::vector<Vertex> vertices = {{{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
                                  {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
                                  {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};

  VkBufferCreateInfo bufferCreateInfo = {};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = size_bytes32(vertices);
  bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer vertexBuffer;
  BB_VK_ASSERT(
      vkCreateBuffer(device, &bufferCreateInfo, nullptr, &vertexBuffer));

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  VkDeviceMemory vertexBufferMemory;
  BB_VK_ASSERT(
      vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory));
  vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);
  {
    void *data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferCreateInfo.size, 0, &data);
    memcpy(data, vertices.data(), bufferCreateInfo.size);
    vkUnmapMemory(device, vertexBufferMemory);
  }

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndices.Graphics.value();
  cmdPoolCreateInfo.flags = 0;

  VkCommandPool graphicsCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr,
                                   &graphicsCmdPool));

  std::vector<VkCommandBuffer> graphicsCmdBuffers(numSwapChainImages);

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.commandPool = graphicsCmdPool;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = (uint32_t)graphicsCmdBuffers.size();
  BB_VK_ASSERT(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo,
                                        graphicsCmdBuffers.data()));

  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    VkCommandBuffer cmdBuffer = graphicsCmdBuffers[i];

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = 0;
    cmdBeginInfo.pInheritanceInfo = nullptr;

    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {0.f, 0.f, 0.f, 1.f};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphicsPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdDraw(cmdBuffer, (uint32_t)vertices.size(), 1, 0, 0);
    vkCmdEndRenderPass(cmdBuffer);

    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
  }

  std::vector<VkSemaphore> imageAvailableSemaphores(numSwapChainImages);
  std::vector<VkSemaphore> renderFinishedSemaphores(numSwapChainImages);
  std::vector<VkFence> imageAvailableFences(numSwapChainImages);

  VkSemaphoreCreateInfo semaphoreCreateInfo = {};
  semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    BB_VK_ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                                   &imageAvailableSemaphores[i]));
    BB_VK_ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                                   &renderFinishedSemaphores[i]));
    BB_VK_ASSERT(vkCreateFence(device, &fenceCreateInfo, nullptr,
                               &imageAvailableFences[i]));
  }

  uint32_t currentFrame = 0;

  Assimp::Importer importer;
  const aiScene *scene =
      importer.ReadFile(resourceRootPath + "ShaderBall.fbx",
                        aiProcess_Triangulate | aiProcess_FlipUVs);

  bool running = true;
  SDL_Event e = {};
  while (running) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        running = false;
      }
    }

    uint32_t imageIndex;
    VkResult acquireNextImageResult = vkAcquireNextImageKHR(
        device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapChain(window, device, graphicsCmdBuffers, graphicsCmdPool,
                      swapChainFramebuffers, graphicsPipeline, renderPass,
                      swapChain, swapChainImageFormat, swapChainImages,
                      swapChainCreateInfo, swapChainSupportDetails,
                      renderPassCreateInfo, pipelineCreateInfo,
                      numSwapChainImages, swapChainExtent, swapChainImageViews,
                      swapChainFramebuffers, cmdPoolCreateInfo);
      continue;
    }

    vkWaitForFences(device, 1, &imageAvailableFences[currentFrame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(device, 1, &imageAvailableFences[currentFrame]);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &graphicsCmdBuffers[currentFrame];

    BB_VK_ASSERT(vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                               imageAvailableFences[currentFrame]));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult queuePresentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        queuePresentResult == VK_SUBOPTIMAL_KHR) {
      updateSwapChain(window, device, graphicsCmdBuffers, graphicsCmdPool,
                      swapChainFramebuffers, graphicsPipeline, renderPass,
                      swapChain, swapChainImageFormat, swapChainImages,
                      swapChainCreateInfo, swapChainSupportDetails,
                      renderPassCreateInfo, pipelineCreateInfo,
                      numSwapChainImages, swapChainExtent, swapChainImageViews,
                      swapChainFramebuffers, cmdPoolCreateInfo);
      continue;
    }

    currentFrame = (currentFrame + 1) % numSwapChainImages;
  }

  vkQueueWaitIdle(graphicsQueue);
  vkQueueWaitIdle(transferQueue);
  vkQueueWaitIdle(presentQueue);

  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroyFence(device, imageAvailableFences[i], nullptr);
  }
  vkFreeCommandBuffers(device, graphicsCmdPool,
                       (uint32_t)graphicsCmdBuffers.size(),
                       graphicsCmdBuffers.data());
  vkDestroyCommandPool(device, graphicsCmdPool, nullptr);
  vkFreeMemory(device, vertexBufferMemory, nullptr);
  vkDestroyBuffer(device, vertexBuffer, nullptr);
  for (VkFramebuffer fb : swapChainFramebuffers) {
    vkDestroyFramebuffer(device, fb, nullptr);
  }
  vkDestroyPipeline(device, graphicsPipeline, nullptr);
  vkDestroyRenderPass(device, renderPass, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyShaderModule(device, testVertShaderModule, nullptr);
  vkDestroyShaderModule(device, testFragShaderModule, nullptr);
  for (VkImageView imageView : swapChainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }
  vkDestroySwapchainKHR(device, swapChain, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  if (messenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    messenger = VK_NULL_HANDLE;
  }
  vkDestroyInstance(instance, nullptr);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}