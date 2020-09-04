#include "external/volk.h"
#include "external/SDL2/SDL.h"
#include "external/SDL2/SDL_main.h"
#include "external/SDL2/SDL_syswm.h"
#include "external/SDL2/SDL_vulkan.h"
#include "external/fmt/format.h"
#include "external/assimp/Importer.hpp"
#include "external/assimp/scene.h"
#include "external/assimp/postprocess.h"
#include <chrono>
#include <algorithm>
#include <stdint.h>
#include <unordered_map>
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
    auto result##__LINE__ = exp;                                               \
    BB_ASSERT(result##__LINE__ == VK_SUCCESS);                                 \
  } while (0)

namespace bb {

template <typename T> uint32_t size_bytes32(const T &_container) {
  return (uint32_t)(sizeof(typename T::value_type) * _container.size());
}

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

  T *begin() { return Elems; }

  T *end() { return Elems + (size_t)(E::COUNT); }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

using Time = std::chrono::time_point<std::chrono::high_resolution_clock>;

Time getCurrentTime() { return std::chrono::high_resolution_clock::now(); }

static_assert(sizeof(Time) <= sizeof(Time *));
float getElapsedTimeInSeconds(Time _start, Time _end) {
  float result = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(
                             _end - _start)
                             .count()) /
                 1000.f;
  return result;
}

constexpr float pi32 = 3.141592f;

float degToRad(float _degrees) { return _degrees * pi32 / 180.f; }

float radToDeg(float _radians) { return _radians * 180.f / pi32; }

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

  static Mat4 translate(const Float3 &_delta) {
    // clang-format off
    return {{
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {_delta.X, _delta.Y, _delta.Z, 1},
    }};
    // clang-format on
  }

  static Mat4 scale(const Float3 &_scale) {
    // clang-format off
    return {{
      {_scale.X, 0, 0, 0},
      {0, _scale.Y, 0, 0},
      {0, 0, _scale.Z, 0},
      {0, 0, 0, 1},
    }};
    // clang-format on
  }

  static Mat4 rotateX(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {1, 0,   0,  0},
      {0, cr,  sr, 0},
      {0, -sr, cr, 0},
      {0, 0,   0,  1},
    }};
    // clang-format on
  };

  static Mat4 rotateY(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {cr,  0, sr, 0},
      {0,   1, 0,  0},
      {-sr, 0, cr, 0},
      {0,   0, 0,  1},
    }};
    // clang-format on
  }

  static Mat4 rotateZ(float _degrees) {
    float radians = degToRad(_degrees);
    float cr = cosf(radians);
    float sr = sinf(radians);
    // clang-format off
    return {{
      {cr,  sr, 0, 0},
      {-sr, cr, 0, 0},
      {0,   0,  1, 0},
      {0,   0,  0, 1},
    }};
    // clang-format on
  }

  static Mat4 lookAt(const Float3 &_eye, const Float3 &_target,
                     const Float3 &_upAxis = {0, 1, 0}) {
    Float3 forward = (_target - _eye).normalize();
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

  static Mat4 perspective(float _fovDegrees, float aspectRatio, float nearZ,
                          float farZ) {
    float d = tan(degToRad(_fovDegrees) * 0.5f);
    float fSubN = farZ - nearZ;
    // clang-format off
    Mat4 result = {{
      {d / aspectRatio, 0, 0,                    0},
      {0,               d, 0,                    0},
      {0,               0, -nearZ / fSubN,       1},
      {0,               0, nearZ * farZ / fSubN, 0},
    }};
    // clang-format on
    return result;
  }
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

struct UniformBlock {
  Mat4 ModelMat;
  Mat4 ViewMat;
  Mat4 ProjMat;
};

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
  std::optional<uint32_t> Compute;

  bool isCompleted() const {
    return Graphics.has_value() && Transfer0.has_value() &&
           Present.has_value() && Compute.has_value();
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
    } else if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
               !result.Compute.has_value()) {
      result.Compute = i;
    } else if (!result.Present.has_value()) {
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

  // Failed to retrieve unique queue family index per queue type - get
  // duplicated queue family index with others
  if (!result.isCompleted()) {
    for (uint32_t i = 0; i < numQueueFamilyProperties; i++) {
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          !result.Graphics.has_value()) {
        result.Graphics = i;
      }
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
          !result.Transfer0.has_value()) {
        result.Transfer0 = i;
      }
      if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
          !result.Compute.has_value()) {
        result.Compute = i;
      }
      if (!result.Present.has_value()) {
        VkBool32 supportPresent = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, i, _surface,
                                             &supportPresent);
        if (supportPresent) {
          result.Present = i;
        }
      }
    }
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
struct Buffer {
  VkBuffer Buffer;
  VkDeviceMemory Memory;
  uint32_t Size;
};
struct SwapChain {
  VkSwapchainKHR Handle;
  VkFormat ImageFormat;
  VkExtent2D Extent;
};
SwapChain
createSwapChain(VkDevice _device, VkSurfaceKHR _surface,
                const SwapChainSupportDetails &_swapChainSupportDetails,
                const uint32_t &_width, const uint32_t &_height,
                const QueueFamilyIndices &_queueFamilyIndices) {
  VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
  swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapChainCreateInfo.surface = _surface;
  swapChainCreateInfo.minImageCount =
      _swapChainSupportDetails.Capabilities.minImageCount + 1;
  if (_swapChainSupportDetails.Capabilities.maxImageCount > 0 &&
      swapChainCreateInfo.minImageCount >
          _swapChainSupportDetails.Capabilities.maxImageCount) {
    swapChainCreateInfo.minImageCount =
        _swapChainSupportDetails.Capabilities.maxImageCount;
  }
  VkSurfaceFormatKHR surfaceFormat =
      _swapChainSupportDetails.chooseSurfaceFormat();
  swapChainCreateInfo.imageFormat = surfaceFormat.format;
  swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
  swapChainCreateInfo.imageExtent =
      _swapChainSupportDetails.chooseExtent(_width, _height);
  swapChainCreateInfo.imageArrayLayers = 1;
  swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t sharedQueueFamilyIndices[2] = {_queueFamilyIndices.Graphics.value(),
                                          _queueFamilyIndices.Present.value()};
  if (_queueFamilyIndices.Graphics != _queueFamilyIndices.Present) {
    // TODO(ilgwon): Can be an interesting optimization to use EXCLUSIVE mode
    // with multiple queues
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapChainCreateInfo.queueFamilyIndexCount = 2;
    swapChainCreateInfo.pQueueFamilyIndices = sharedQueueFamilyIndices;
  } else {
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  swapChainCreateInfo.preTransform =
      _swapChainSupportDetails.Capabilities.currentTransform;
  swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapChainCreateInfo.presentMode =
      _swapChainSupportDetails.choosePresentMode();
  swapChainCreateInfo.clipped = VK_TRUE;
  swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

  SwapChain swapChain;
  swapChain.ImageFormat = swapChainCreateInfo.imageFormat;
  swapChain.Extent = swapChainCreateInfo.imageExtent;
  BB_VK_ASSERT(vkCreateSwapchainKHR(_device, &swapChainCreateInfo, nullptr,
                                    &swapChain.Handle));

  return swapChain;
}
void recordCommand(const uint32_t &_numSwapChainImages,
                   std::vector<VkCommandBuffer> &_graphicsCmdBuffers,
                   const VkRenderPass &_renderPass,
                   const std::vector<VkFramebuffer> &_swapChainFramebuffers,
                   const VkExtent2D &_swapChainExtent,
                   const VkPipeline &_graphicsPipeline,
                   const Buffer &_vertexBuffer, const Buffer &_indexBuffer,
                   const VkPipelineLayout &_pipelineLayout,
                   const std::vector<VkDescriptorSet> &_descriptorSets,
                   const std::vector<uint32_t> &_indices) {
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkCommandBuffer &cmdBuffer = _graphicsCmdBuffers[i];

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = 0;
    cmdBeginInfo.pInheritanceInfo = nullptr;

    BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = _swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapChainExtent;

    VkClearValue clearColor = {0.f, 0.f, 0.f, 1.f};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _graphicsPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &_vertexBuffer.Buffer, &offset);
    vkCmdBindIndexBuffer(cmdBuffer, _indexBuffer.Buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipelineLayout, 0, 1, &_descriptorSets[i], 0,
                            nullptr);
    vkCmdDrawIndexed(cmdBuffer, (uint32_t)_indices.size(), 1, 0, 0, 0);
    vkCmdEndRenderPass(cmdBuffer);

    BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));
  }
}

// Important : You need to delete every cmd used by swapchain
// through queue. Dont forget to add it here too when you add another cmd.
void updateSwapChain(SDL_Window *_window, VkDevice _device,
                     std::vector<VkCommandBuffer> &_graphicsCmdBuffers,
                     VkCommandPool &_graphicsCmdPool,
                     std::vector<VkFramebuffer> &_swapChainFramebuffers,
                     VkPipeline &_graphicsPipeline, VkRenderPass &_renderPass,
                     SwapChain &_swapChain, VkSurfaceKHR &_surface,
                     const QueueFamilyIndices &_queueFamilyIndices,
                     std::vector<VkImage> &_swapChainImages,
                     const SwapChainSupportDetails &_swapChainSupportDetails,
                     const VkRenderPassCreateInfo &_renderPassCreateInfo,
                     VkViewport &_viewPort,
                     VkGraphicsPipelineCreateInfo &_graphicsPipelineCreateInfo,
                     uint32_t &_numSwapChainImages,
                     std::vector<VkImageView> &_swapChainImageViews,
                     std::vector<VkFramebuffer> &_SwapChainFramebuffers,
                     const VkCommandPoolCreateInfo &_cmdPoolCreateInfo,
                     const std::vector<Vertex> &_vertices,
                     Buffer &_vertexBuffer,
                     const std::vector<uint32_t> &_indices,
                     Buffer &_indexBuffer, VkPipelineLayout &_pipelineLayout,
                     const std::vector<VkDescriptorSet> &_descriptorSets) {
  int width = 0, height = 0;

  if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
    SDL_WaitEvent(nullptr);

  SDL_GetWindowSize(_window, &width, &height);

  vkDeviceWaitIdle(_device); // Ensure that device finished using swap chain.

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
  vkDestroySwapchainKHR(_device, _swapChain.Handle, nullptr);
  // Destroy any other buffers used by queues here, including uniform buffers.

  // recreate
  _swapChain = createSwapChain(_device, _surface, _swapChainSupportDetails,
                               width, height, _queueFamilyIndices);

  vkGetSwapchainImagesKHR(_device, _swapChain.Handle, &_numSwapChainImages,
                          nullptr);
  _swapChainImages.resize(_numSwapChainImages);
  vkGetSwapchainImagesKHR(_device, _swapChain.Handle, &_numSwapChainImages,
                          _swapChainImages.data());
  _swapChainImageViews.resize(_numSwapChainImages);
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkImageViewCreateInfo swapChainImageViewCreateInfo = {};
    swapChainImageViewCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    swapChainImageViewCreateInfo.image = _swapChainImages[i];
    swapChainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    swapChainImageViewCreateInfo.format = _swapChain.ImageFormat;
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

  _viewPort.width = width;
  _viewPort.height = height;
  _graphicsPipelineCreateInfo.renderPass = _renderPass;
  BB_VK_ASSERT(vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
                                         &_graphicsPipelineCreateInfo, nullptr,
                                         &_graphicsPipeline));

  _SwapChainFramebuffers.resize(_numSwapChainImages);
  for (uint32_t i = 0; i < _numSwapChainImages; ++i) {
    VkFramebufferCreateInfo fbCreateInfo = {};
    fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCreateInfo.renderPass = _renderPass;
    fbCreateInfo.attachmentCount = 1;
    fbCreateInfo.pAttachments = &_swapChainImageViews[i];
    fbCreateInfo.width = _swapChain.Extent.width;
    fbCreateInfo.height = _swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(_device, &fbCreateInfo, nullptr,
                                     &_SwapChainFramebuffers[i]));
  }

  BB_VK_ASSERT(vkResetCommandPool(_device, _graphicsCmdPool, 0));
  recordCommand(_numSwapChainImages, _graphicsCmdBuffers, _renderPass,
                _swapChainFramebuffers, _swapChain.Extent, _graphicsPipeline,
                _vertexBuffer, _indexBuffer, _pipelineLayout, _descriptorSets,
                _indices);
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

Buffer createBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                    VkDeviceSize _size, VkBufferUsageFlags _usage,
                    VkMemoryPropertyFlags _properties) {
  Buffer result = {};

  VkBufferCreateInfo bufferCreateInfo = {};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = _size;
  bufferCreateInfo.usage = _usage;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  BB_VK_ASSERT(
      vkCreateBuffer(_device, &bufferCreateInfo, nullptr, &result.Buffer));

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(_device, result.Buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      _physicalDevice, memRequirements.memoryTypeBits, _properties);

  BB_VK_ASSERT(vkAllocateMemory(_device, &allocInfo, nullptr, &result.Memory));
  BB_VK_ASSERT(vkBindBufferMemory(_device, result.Buffer, result.Memory, 0));

  result.Size = _size;

  return result;
};

Buffer createStagingBuffer(VkDevice _device, VkPhysicalDevice _physicalDevice,
                           const Buffer &_orgBuffer) {
  Buffer result = createBuffer(_device, _physicalDevice, _orgBuffer.Size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  return result;
}

void destroyBuffer(VkDevice _device, Buffer &_buffer) {
  vkDestroyBuffer(_device, _buffer.Buffer, nullptr);
  _buffer.Buffer = VK_NULL_HANDLE;
  vkFreeMemory(_device, _buffer.Memory, nullptr);
  _buffer.Memory = VK_NULL_HANDLE;
}

void copyBuffer(VkDevice _device, VkCommandPool _cmdPool, VkQueue _queue,
                Buffer &_dstBuffer, Buffer &_srcBuffer, VkDeviceSize _size) {
  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = _cmdPool;
  cmdBufferAllocInfo.commandBufferCount = 1;
  VkCommandBuffer cmdBuffer;
  BB_VK_ASSERT(
      vkAllocateCommandBuffers(_device, &cmdBufferAllocInfo, &cmdBuffer));

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  BB_VK_ASSERT(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

  VkBufferCopy copyRegion = {};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = _size;
  vkCmdCopyBuffer(cmdBuffer, _srcBuffer.Buffer, _dstBuffer.Buffer, 1,
                  &copyRegion);

  BB_VK_ASSERT(vkEndCommandBuffer(cmdBuffer));

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmdBuffer;
  vkQueueSubmit(_queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(_queue);

  vkFreeCommandBuffers(_device, cmdBufferAllocInfo.commandPool, 1, &cmdBuffer);
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
      height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

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
  std::unordered_map<uint32_t, uint32_t> queueMap;
  uint32_t maxNumQueues = 0;
  auto incrementNumQueues = [&](uint32_t queueFamilyIndex) {
    auto it = queueMap.find(queueFamilyIndex);
    if (it == queueMap.end()) {
      it = queueMap.emplace(queueFamilyIndex, 1).first;
    } else {
      ++it->second;
    }
    maxNumQueues = std::max(maxNumQueues, it->second);
  };

  incrementNumQueues(queueFamilyIndices.Graphics.value());
  incrementNumQueues(queueFamilyIndices.Transfer0.value());
  incrementNumQueues(queueFamilyIndices.Present.value());
  incrementNumQueues(queueFamilyIndices.Compute.value());

  std::vector<float> queuePriorities(maxNumQueues, 1.f);
  queueCreateInfos.reserve(queueMap.size());
  std::unordered_map<uint32_t, uint32_t> obtainedQueueCounters;

  for (auto [queueFamilyIndex, numQueues] : queueMap) {
    VkDeviceQueueCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndex;
    createInfo.queueCount = numQueues;
    createInfo.pQueuePriorities = queuePriorities.data();
    queueCreateInfos.push_back(createInfo);

    obtainedQueueCounters.emplace(queueFamilyIndex, 0);
  }

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
  VkQueue computeQueue;
  vkGetDeviceQueue(device, queueFamilyIndices.Graphics.value(),
                   obtainedQueueCounters[queueFamilyIndices.Graphics.value()]++,
                   &graphicsQueue);
  vkGetDeviceQueue(
      device, queueFamilyIndices.Transfer0.value(),
      obtainedQueueCounters[queueFamilyIndices.Transfer0.value()]++,
      &transferQueue);
  vkGetDeviceQueue(device, queueFamilyIndices.Present.value(),
                   obtainedQueueCounters[queueFamilyIndices.Present.value()]++,
                   &presentQueue);
  vkGetDeviceQueue(device, queueFamilyIndices.Compute.value(),
                   obtainedQueueCounters[queueFamilyIndices.Compute.value()]++,
                   &computeQueue);
  BB_ASSERT(graphicsQueue != VK_NULL_HANDLE &&
            transferQueue != VK_NULL_HANDLE && presentQueue != VK_NULL_HANDLE &&
            computeQueue != VK_NULL_HANDLE);

  SwapChain swapChain =
      createSwapChain(device, surface, swapChainSupportDetails, width, height,
                      queueFamilyIndices);

  uint32_t numSwapChainImages;
  std::vector<VkImage> swapChainImages;
  vkGetSwapchainImagesKHR(device, swapChain.Handle, &numSwapChainImages,
                          nullptr);
  swapChainImages.resize(numSwapChainImages);
  vkGetSwapchainImagesKHR(device, swapChain.Handle, &numSwapChainImages,
                          swapChainImages.data());

  std::vector<VkImageView> swapChainImageViews(numSwapChainImages);
  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    VkImageViewCreateInfo swapChainImageViewCreateInfo = {};
    swapChainImageViewCreateInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    swapChainImageViewCreateInfo.image = swapChainImages[i];
    swapChainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    swapChainImageViewCreateInfo.format = swapChain.ImageFormat;
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
  viewport.width = (float)swapChain.Extent.width;
  viewport.height = (float)swapChain.Extent.height;
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapChain.Extent;

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

  VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
  descriptorSetLayoutBinding.binding = 0;
  descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorSetLayoutBinding.descriptorCount = 1;
  descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  descriptorSetLayoutBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = 1;
  descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;

  VkDescriptorSetLayout descriptorSetLayout;
  BB_VK_ASSERT(vkCreateDescriptorSetLayout(
      device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCreateInfo.setLayoutCount = 1;
  pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
  pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

  VkPipelineLayout pipelineLayout;
  BB_VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo,
                                      nullptr, &pipelineLayout));

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = swapChain.ImageFormat;
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
    fbCreateInfo.width = swapChain.Extent.width;
    fbCreateInfo.height = swapChain.Extent.height;
    fbCreateInfo.layers = 1;

    BB_VK_ASSERT(vkCreateFramebuffer(device, &fbCreateInfo, nullptr,
                                     &swapChainFramebuffers[i]));
  }

  VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
  cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndices.Graphics.value();
  cmdPoolCreateInfo.flags = 0;

  VkCommandPool graphicsCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr,
                                   &graphicsCmdPool));

  cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndices.Transfer0.value();
  cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VkCommandPool transferCmdPool;
  BB_VK_ASSERT(vkCreateCommandPool(device, &cmdPoolCreateInfo, nullptr,
                                   &transferCmdPool));

  std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                  {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
                                  {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                                  {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};

  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

  Buffer vertexBuffer = createBuffer(
      device, physicalDevice, size_bytes32(vertices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Buffer vertexStagingBuffer =
      createStagingBuffer(device, physicalDevice, vertexBuffer);

  Buffer indexBuffer = createBuffer(
      device, physicalDevice, size_bytes32(indices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Buffer indexStagingBuffer =
      createStagingBuffer(device, physicalDevice, indexBuffer);

  {
    void *data;
    vkMapMemory(device, vertexStagingBuffer.Memory, 0, vertexStagingBuffer.Size,
                0, &data);
    memcpy(data, vertices.data(), vertexStagingBuffer.Size);
    vkUnmapMemory(device, vertexStagingBuffer.Memory);
    vkMapMemory(device, indexStagingBuffer.Memory, 0, indexStagingBuffer.Size,
                0, &data);
    memcpy(data, indices.data(), indexStagingBuffer.Size);
    vkUnmapMemory(device, indexStagingBuffer.Memory);

    copyBuffer(device, transferCmdPool, transferQueue, vertexBuffer,
               vertexStagingBuffer, vertexStagingBuffer.Size);
    copyBuffer(device, transferCmdPool, transferQueue, indexBuffer,
               indexStagingBuffer, indexStagingBuffer.Size);
  }

  std::vector<Buffer> uniformBuffers(numSwapChainImages);
  for (Buffer &uniformBuffer : uniformBuffers) {
    uniformBuffer = createBuffer(device, physicalDevice, sizeof(UniformBlock),
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }

  VkDescriptorPoolSize descriptorPoolSize = {};
  descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorPoolSize.descriptorCount = numSwapChainImages;
  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
  descriptorPoolCreateInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.poolSizeCount = 1;
  descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
  descriptorPoolCreateInfo.maxSets = numSwapChainImages;

  VkDescriptorPool descriptorPool;
  BB_VK_ASSERT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo,
                                      nullptr, &descriptorPool));

  std::vector<VkDescriptorSet> descriptorSets(numSwapChainImages);
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts(numSwapChainImages,
                                                          descriptorSetLayout);
  VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = descriptorPool;
  descriptorSetAllocInfo.descriptorSetCount = numSwapChainImages;
  descriptorSetAllocInfo.pSetLayouts = descriptorSetLayouts.data();
  BB_VK_ASSERT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo,
                                        descriptorSets.data()));
  for (uint32_t i = 0; i < numSwapChainImages; ++i) {
    VkDescriptorBufferInfo descriptorBufferInfo = {};
    descriptorBufferInfo.buffer = uniformBuffers[i].Buffer;
    descriptorBufferInfo.offset = 0;
    descriptorBufferInfo.range = sizeof(UniformBlock);

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSets[i];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &descriptorBufferInfo;
    write.pImageInfo = nullptr;
    write.pTexelBufferView = nullptr;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }

  std::vector<VkCommandBuffer> graphicsCmdBuffers(numSwapChainImages);

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandPool = graphicsCmdPool;
  cmdBufferAllocInfo.commandBufferCount = (uint32_t)graphicsCmdBuffers.size();
  BB_VK_ASSERT(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo,
                                        graphicsCmdBuffers.data()));

  recordCommand(numSwapChainImages, graphicsCmdBuffers, renderPass,
                swapChainFramebuffers, swapChain.Extent, graphicsPipeline,
                vertexBuffer, indexBuffer, pipelineLayout, descriptorSets,
                indices);

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

  Time lastTime = getCurrentTime();

  SDL_Event e = {};
  while (running) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        running = false;
      }
    }

    Time currentTime = getCurrentTime();
    float dt = getElapsedTimeInSeconds(lastTime, currentTime);
    lastTime = currentTime;

    uint32_t imageIndex;
    VkResult acquireNextImageResult = vkAcquireNextImageKHR(
        device, swapChain.Handle, UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physicalDevice, surface, &swapChainSupportDetails.Capabilities);

      updateSwapChain(window, device, graphicsCmdBuffers, graphicsCmdPool,
                      swapChainFramebuffers, graphicsPipeline, renderPass,
                      swapChain, surface, queueFamilyIndices, swapChainImages,
                      swapChainSupportDetails, renderPassCreateInfo, viewport,
                      pipelineCreateInfo, numSwapChainImages,
                      swapChainImageViews, swapChainFramebuffers,
                      cmdPoolCreateInfo, vertices, vertexBuffer, indices,
                      indexBuffer, pipelineLayout, descriptorSets);
      continue;
    }

    vkWaitForFences(device, 1, &imageAvailableFences[currentFrame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(device, 1, &imageAvailableFences[currentFrame]);

    static float angle = 0;
    angle += 30.f * dt;
    if (angle > 360.f) {
      angle -= 360.f;
    }
    UniformBlock uniformBlock = {};
    uniformBlock.ModelMat = Mat4::rotateZ(angle);
    uniformBlock.ViewMat = Mat4::lookAt({3, 0, -3}, {0, 0, 0});
    uniformBlock.ProjMat =
        Mat4::perspective(90.f, (float)width / (float)height, 0.1f, 1000.f);
    {
      void *data;
      vkMapMemory(device, uniformBuffers[currentFrame].Memory, 0,
                  sizeof(UniformBlock), 0, &data);
      memcpy(data, &uniformBlock, sizeof(UniformBlock));
      vkUnmapMemory(device, uniformBuffers[currentFrame].Memory);
    }

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
    presentInfo.pSwapchains = &swapChain.Handle;
    presentInfo.pImageIndices = &imageIndex;

    VkResult queuePresentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        queuePresentResult == VK_SUBOPTIMAL_KHR) {
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physicalDevice, surface, &swapChainSupportDetails.Capabilities);

      updateSwapChain(window, device, graphicsCmdBuffers, graphicsCmdPool,
                      swapChainFramebuffers, graphicsPipeline, renderPass,
                      swapChain, surface, queueFamilyIndices, swapChainImages,
                      swapChainSupportDetails, renderPassCreateInfo, viewport,
                      pipelineCreateInfo, numSwapChainImages,
                      swapChainImageViews, swapChainFramebuffers,
                      cmdPoolCreateInfo, vertices, vertexBuffer, indices,
                      indexBuffer, pipelineLayout, descriptorSets);
    }

    currentFrame = (currentFrame + 1) % numSwapChainImages;
  }

  vkQueueWaitIdle(computeQueue);
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

  vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  for (Buffer &uniformBuffer : uniformBuffers) {
    destroyBuffer(device, uniformBuffer);
  }
  destroyBuffer(device, indexBuffer);
  destroyBuffer(device, indexStagingBuffer);
  destroyBuffer(device, vertexStagingBuffer);
  destroyBuffer(device, vertexBuffer);

  vkDestroyCommandPool(device, transferCmdPool, nullptr);
  vkDestroyCommandPool(device, graphicsCmdPool, nullptr);

  for (VkFramebuffer fb : swapChainFramebuffers) {
    vkDestroyFramebuffer(device, fb, nullptr);
  }
  vkDestroyPipeline(device, graphicsPipeline, nullptr);
  vkDestroyRenderPass(device, renderPass, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  vkDestroyShaderModule(device, testVertShaderModule, nullptr);
  vkDestroyShaderModule(device, testFragShaderModule, nullptr);
  for (VkImageView imageView : swapChainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }
  vkDestroySwapchainKHR(device, swapChain.Handle, nullptr);
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