#pragma once
#include "render.h"
#include "external/imgui/imgui.h"

namespace bb {
struct Gizmo {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;
  Buffer VertexBuffer;
  Buffer IndexBuffer;
  uint32_t NumIndices;

  int ViewportExtent = 100;
};

enum class GBufferVisualizingOption {
  Position,
  Normal,
  Albedo,
  MRHA,
  MaterialIndex,
  RenderedScene,
  COUNT
};

struct GBufferVisualize {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;

  VkExtent2D ViewportExtent;

  StandardPipelineLayout PipelineLayout;

  EnumArray<GBufferVisualizingOption, const char *> OptionLabels = {
      "Position", "Normal",         "Albedo",
      "MRHA",     "Material index", "Rendered Scene"};
  GBufferVisualizingOption CurrentOption =
      GBufferVisualizingOption::RenderedScene;
};

struct LightSources {
  VkPipeline Pipeline;
  Shader VertShader;
  Shader FragShader;
  Buffer VertexBuffer;
  Buffer IndexBuffer;
  uint32_t NumIndices;
  Buffer InstanceBuffer;
  uint32_t NumLights;
};

enum class RenderPassType { Forward, Deferred, COUNT };

// CommonSceneResources doesn't own actual resources, but only references of
// them.
struct CommonSceneResources {
  SDL_Window *Window;
  Renderer *Renderer;
  VkCommandPool TransientCmdPool;

  StandardPipelineLayout *StandardPipelineLayout;
  PBRMaterialSet *MaterialSet;

  SwapChain *SwapChain;

  RenderPass *RenderPass;
  std::vector<VkFramebuffer> *Framebuffers;
  EnumArray<GBufferAttachmentType, Image> *GBufferAttachmentImages;

  VkPipeline GBufferPipeline;
  VkPipeline DeferredBrdfPipeline;
  VkPipeline ForwardBrdfPipeline;

  std::vector<Frame> *Frames;
  std::vector<FrameSync> *FrameSyncObjects;

  Gizmo *Gizmo;
  GBufferVisualize *GBufferVisualize;
};

struct SceneBase {
  CommonSceneResources *Common;
  RenderPassType SceneRenderPassType = RenderPassType::Deferred;

  explicit SceneBase(CommonSceneResources *_common) : Common(_common) {}
  virtual ~SceneBase() = default;
  virtual void updateGUI(float _dt) = 0;
  virtual void updateScene(float _dt) = 0;
  virtual void drawScene(const Frame &_frame) = 0;

  template <typename Container>
  Buffer createVertexBuffer(const Container &_vertices) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_vertices), Vertex>,
                  "Element type for _vertices is not Vertex!");
    const Renderer &renderer = *Common->Renderer;
    VkCommandPool transientCmdPool = Common->TransientCmdPool;
    Buffer vertexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeBytes32(_vertices), std::data(_vertices));
    return vertexBuffer;
  }

  template <typename Container>
  Buffer createIndexBuffer(const Container &_indices) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_indices), uint32_t>,
                  "Element type for _indices is not uint32_t!");
    const Renderer &renderer = *Common->Renderer;
    VkCommandPool transientCmdPool = Common->TransientCmdPool;
    Buffer indexBuffer = createDeviceLocalBufferFromMemory(
        renderer, transientCmdPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeBytes32(_indices), std::data(_indices));
    return indexBuffer;
  }

  Buffer createInstanceBuffer(uint32_t _numInstances) const {
    const Renderer &renderer = *Common->Renderer;
    Buffer instanceBuffer =
        createBuffer(renderer, sizeof(InstanceBlock) * _numInstances,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    return instanceBuffer;
  }

  template <typename Container>
  void updateInstanceBufferMemory(const Buffer &_instanceBuffer,
                                  const Container &_instanceData) const {
    static_assert(std::is_same_v<ELEMENT_TYPE(_instanceData), InstanceBlock>,
                  "Element type for _instanceData is not InstanceBlock!");
    const Renderer &renderer = *Common->Renderer;

    void *dst;
    vkMapMemory(renderer.Device, _instanceBuffer.Memory, 0,
                _instanceBuffer.Size, 0, &dst);
    memcpy(dst, std::data(_instanceData), _instanceBuffer.Size);
    vkUnmapMemory(renderer.Device, _instanceBuffer.Memory);
  }
};

struct ShaderBallScene : SceneBase {
  struct {
    Buffer VertexBuffer;
    Buffer IndexBuffer;
    uint32_t NumIndices;

    uint32_t NumInstances = 1;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;
  } Plane;

  struct {
    Buffer VertexBuffer;
    uint32_t NumVertices;

    uint32_t NumInstances = 30;
    std::vector<InstanceBlock> InstanceData;
    Buffer InstanceBuffer;

    float Angle = -90;
  } ShaderBall;

  struct {
    EnumArray<PBRMapType, ImTextureID> DefaultMaterialTextureId;
    std::vector<EnumArray<PBRMapType, ImTextureID>> MaterialTextureIds;
    int SelectedMaterial = 1;
    int SelectedShaderBallInstance = -1;
  } GUI;

  explicit ShaderBallScene(CommonSceneResources *_common);
  ~ShaderBallScene() override;
  void updateGUI(float _dt) override;
  void updateScene(float _dt) override;
  void drawScene(const Frame &_frame) override;
};

} // namespace bb