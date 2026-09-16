// C-ABI-compatible mirror of NVIDIA Streamline SDK's C++ structs, hand-written
// against the headers in external/streamline/include (SDK v2.14.1) -- this
// project's rendering code is C, not C++, so these structs cannot include
// sl.h/sl_core_types.h/sl_dlss.h directly (SL_STRUCT_BEGIN generates C++
// classes with inheritance from sl::BaseStructure, plus some structs like
// sl::FrameToken have a virtual operator, which has no C representation).
//
// Every struct below matches the memory layout of its sl:: counterpart
// exactly -- same field order, same sizes -- since sl::BaseStructure's
// inheritance is standard-layout single inheritance (base fields first,
// no virtual functions on BaseStructure itself), so a C struct with the
// same fields in the same order is ABI-compatible. sl::StructType is a
// 16-byte GUID-shaped value (uint32+uint16+uint16+uint8[8]), identical
// layout to Vulkan's own GUID-shaped types.
//
// DO NOT hand-edit field order/sizes without re-checking against the real
// headers in external/streamline/include -- a mismatch here is a silent
// ABI bug, not a compile error, since the DLL has no way to validate our
// struct layout beyond structType/structVersion.
//
// sl::FrameToken is NOT mirrored here -- it has a virtual member function
// (non-standard-layout, no valid C equivalent) and per the SDK's own
// documented usage pattern is always obtained via slGetNewFrameToken and
// only ever passed around as an opaque pointer, never constructed or
// inspected by the host -- so vk_dlss.c only ever holds it as a `void*`.

#ifndef VK_DLSS_TYPES_H
#define VK_DLSS_TYPES_H

#include <stdint.h>

typedef struct sl_struct_type_s {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
} sl_struct_type_t;

// Mirrors sl::BaseStructure -- every SL_STRUCT_BEGIN type starts with these
// three fields in this order.
#define SL_BASE_STRUCTURE_FIELDS \
	void* next; \
	sl_struct_type_t structType; \
	size_t structVersion

typedef struct sl_base_structure_s {
	SL_BASE_STRUCTURE_FIELDS;
} sl_base_structure_t;

typedef int32_t sl_result_t;
typedef uint32_t sl_feature_t;
typedef uint32_t sl_buffer_type_t;

// sl::Feature values (sl_core_types.h)
#define SL_FEATURE_DLSS 0

// sl::BufferType values (sl_core_types.h) -- only the ones this project uses.
#define SL_BUFFER_TYPE_DEPTH 0
#define SL_BUFFER_TYPE_MOTION_VECTORS 1
#define SL_BUFFER_TYPE_SCALING_INPUT_COLOR 3
#define SL_BUFFER_TYPE_SCALING_OUTPUT_COLOR 4
#define SL_BUFFER_TYPE_EXPOSURE 13

// sl::Result: eOk == 0, every other value is an error (sl_result.h) -- this
// project only ever checks for success/failure, never a specific error
// code, so the individual enum values aren't mirrored.
#define SL_RESULT_OK 0

// sl::Boolean (sl_consts.h)
typedef enum sl_boolean_e {
	SL_FALSE = 0,
	SL_TRUE = 1,
	SL_INVALID = 2,
} sl_boolean_t;

// sl::LogLevel (sl_core_types.h) -- only the values this project sets.
typedef enum sl_log_level_e {
	SL_LOG_LEVEL_OFF = 0,
	SL_LOG_LEVEL_DEFAULT = 1,
	SL_LOG_LEVEL_VERBOSE = 2,
} sl_log_level_t;

// sl::RenderAPI (sl_core_types.h)
typedef enum sl_render_api_e {
	SL_RENDER_API_D3D11 = 0,
	SL_RENDER_API_D3D12 = 1,
	SL_RENDER_API_VULKAN = 2,
} sl_render_api_t;

// sl::PreferenceFlags values (sl_core_types.h) -- the enum itself is a
// uint64_t (`enum class PreferenceFlags : uint64_t`), not the uint32_t this
// file assumed in an earlier draft; Preferences.flags below is typed
// uint64_t accordingly. Only the flags this project actually sets are
// named.
#define SL_PREF_DISABLE_CL_STATE_TRACKING ((uint64_t)1 << 0)
#define SL_PREF_ALLOW_OTA ((uint64_t)1 << 3)
#define SL_PREF_LOAD_DOWNLOADED_PLUGINS ((uint64_t)1 << 4)

// Mirrors sl::Preferences (sl_core_types.h, StructVersion1). showConsole is
// a C++ `bool` (1 byte with MSVC) followed by 3 bytes of struct padding so
// logLevel (an enum, 4-byte aligned) starts on the next 4-byte boundary --
// same padding reasoning as sl_vulkan_info_t's useNativeOpticalFlowMode.
typedef struct sl_preferences_s {
	SL_BASE_STRUCTURE_FIELDS;
	uint8_t showConsole;
	uint8_t _pad0[3];
	sl_log_level_t logLevel;
	const wchar_t** pathsToPlugins;
	uint32_t numPathsToPlugins;
	const wchar_t* pathToLogsAndData;
	void* allocateCallback;
	void* releaseCallback;
	void* logMessageCallback;
	uint64_t flags;
	const sl_feature_t* featuresToLoad;
	uint32_t numFeaturesToLoad;
	uint32_t applicationId;
	uint32_t engine; // sl::EngineType
	const char* engineVersion;
	const char* projectId;
	sl_render_api_t renderAPI;
} sl_preferences_t;

// Mirrors sl::AdapterInfo (sl_core_types.h, StructVersion1).
typedef struct sl_adapter_info_s {
	SL_BASE_STRUCTURE_FIELDS;
	uint8_t* deviceLUID;
	uint32_t deviceLUIDSizeInBytes;
	void* vkPhysicalDevice;
} sl_adapter_info_t;

// Mirrors sl::VulkanInfo (sl_helpers_vk.h, StructVersion3) EXACTLY -- field
// order/sizes verified against the real header, including the C++ `bool`
// (1 byte, not 4 -- unlike sl::Boolean which is a 1-byte enum too, so no
// mismatch there, but different from this project's own `qbool`/int
// convention, hence the explicit uint8_t here).
typedef struct sl_vulkan_info_s {
	SL_BASE_STRUCTURE_FIELDS;
	void* device;
	void* instance;
	void* physicalDevice;
	uint32_t computeQueueIndex;
	uint32_t computeQueueFamily;
	uint32_t graphicsQueueIndex;
	uint32_t graphicsQueueFamily;
	uint32_t opticalFlowQueueIndex;
	uint32_t opticalFlowQueueFamily;
	uint8_t useNativeOpticalFlowMode; // C++ bool is 1 byte with MSVC
	uint8_t _pad0[3]; // C++ struct padding to the next uint32_t's 4-byte alignment
	uint32_t computeQueueCreateFlags;
	uint32_t graphicsQueueCreateFlags;
	uint32_t opticalFlowQueueCreateFlags;
} sl_vulkan_info_t;

// Mirrors sl::Resource (sl_core_types.h, StructVersion1). `type` is
// sl::ResourceType, `enum class ResourceType : char` -- 1 byte, NOT a
// uint32_t -- so 3 bytes of padding follow to keep `native` (a pointer,
// 8-byte aligned) on its natural boundary.
typedef struct sl_resource_s {
	SL_BASE_STRUCTURE_FIELDS;
	uint8_t type; // sl::ResourceType::eTex2d = 0 (only type this project uses)
	uint8_t _pad0[7]; // pad to 8-byte alignment for the pointer that follows
	void* native;
	void* memory;
	void* view;
	uint32_t state; // VkImageLayout, cast to uint32_t
	uint32_t width;
	uint32_t height;
	uint32_t nativeFormat;
	uint32_t mipLevels;
	uint32_t arrayLayers;
	uint64_t gpuVirtualAddress;
	uint32_t flags;
	uint32_t usage;
	uint16_t internalFlags;
	uint16_t reserved;
} sl_resource_t;

// Mirrors sl::Extent (sl_core_types.h).
typedef struct sl_extent_s {
	uint32_t left;
	uint32_t top;
	uint32_t width;
	uint32_t height;
} sl_extent_t;

typedef enum sl_resource_lifecycle_e {
	SL_RESOURCE_LIFECYCLE_VALID_UNTIL_PRESENT = 0,
	SL_RESOURCE_LIFECYCLE_VALID_UNTIL_EVALUATE = 1,
	SL_RESOURCE_LIFECYCLE_ONLY_VALID_NOW = 2,
} sl_resource_lifecycle_t;

// Mirrors sl::ResourceTag (sl_core_types.h, StructVersion1).
typedef struct sl_resource_tag_s {
	SL_BASE_STRUCTURE_FIELDS;
	sl_resource_t* resource;
	sl_buffer_type_t type;
	sl_resource_lifecycle_t lifecycle;
	sl_extent_t extent;
} sl_resource_tag_t;

// Mirrors sl::float2/float3/float4x4 (sl_struct.h) -- plain aggregates, same
// layout as the C++ versions (which are themselves plain float arrays with
// named-member convenience accessors we don't need here).
typedef struct sl_float2_s { float x, y; } sl_float2_t;
typedef struct sl_float3_s { float x, y, z; } sl_float3_t;
typedef struct sl_float4x4_s { float row0[4], row1[4], row2[4], row3[4]; } sl_float4x4_t;

// Mirrors sl::Constants (sl_consts.h, StructVersion2).
typedef struct sl_constants_s {
	SL_BASE_STRUCTURE_FIELDS;
	sl_float4x4_t cameraViewToClip;
	sl_float4x4_t clipToCameraView;
	sl_float4x4_t clipToLensClip;
	sl_float4x4_t clipToPrevClip;
	sl_float4x4_t prevClipToClip;
	sl_float2_t jitterOffset;
	sl_float2_t mvecScale;
	sl_float2_t cameraPinholeOffset;
	sl_float3_t cameraPos;
	sl_float3_t cameraUp;
	sl_float3_t cameraRight;
	sl_float3_t cameraFwd;
	float cameraNear;
	float cameraFar;
	float cameraFOV;
	float cameraAspectRatio;
	float motionVectorsInvalidValue;
	sl_boolean_t depthInverted;
	sl_boolean_t cameraMotionIncluded;
	sl_boolean_t motionVectors3D;
	sl_boolean_t reset;
	sl_boolean_t orthographicProjection;
	sl_boolean_t motionVectorsDilated;
	sl_boolean_t motionVectorsJittered;
	float minRelativeLinearDepthObjectSeparation;
} sl_constants_t;

// Mirrors sl::ViewportHandle (sl_core_types.h, StructVersion1) -- normally
// constructed via a uint32_t-taking C++ constructor; this project fills the
// base fields with SL_INIT_BASE_STRUCTURE below instead.
typedef struct sl_viewport_handle_s {
	SL_BASE_STRUCTURE_FIELDS;
	uint32_t value;
} sl_viewport_handle_t;

typedef enum sl_dlss_mode_e {
	SL_DLSS_MODE_OFF = 0,
	SL_DLSS_MODE_MAX_PERFORMANCE = 1,
	SL_DLSS_MODE_BALANCED = 2,
	SL_DLSS_MODE_MAX_QUALITY = 3,
	SL_DLSS_MODE_ULTRA_PERFORMANCE = 4,
	SL_DLSS_MODE_ULTRA_QUALITY = 5,
	SL_DLSS_MODE_DLAA = 6,
} sl_dlss_mode_t;

typedef enum sl_dlss_preset_e {
	SL_DLSS_PRESET_DEFAULT = 0,
	SL_DLSS_PRESET_K = 11,
	SL_DLSS_PRESET_L = 12,
	SL_DLSS_PRESET_M = 13,
} sl_dlss_preset_t;

// Mirrors sl::DLSSOptions (sl_dlss.h, StructVersion3).
typedef struct sl_dlss_options_s {
	SL_BASE_STRUCTURE_FIELDS;
	sl_dlss_mode_t mode;
	uint32_t outputWidth;
	uint32_t outputHeight;
	float sharpness; // deprecated field, always 0
	float preExposure;
	float exposureScale;
	sl_boolean_t colorBuffersHDR;
	sl_boolean_t indicatorInvertAxisX;
	sl_boolean_t indicatorInvertAxisY;
	sl_dlss_preset_t dlaaPreset;
	sl_dlss_preset_t qualityPreset;
	sl_dlss_preset_t balancedPreset;
	sl_dlss_preset_t performancePreset;
	sl_dlss_preset_t ultraPerformancePreset;
	sl_dlss_preset_t ultraQualityPreset;
	sl_boolean_t useAutoExposure;
	sl_boolean_t alphaUpscalingEnabled;
} sl_dlss_options_t;

// Mirrors sl::DLSSOptimalSettings (sl_dlss.h, StructVersion1).
typedef struct sl_dlss_optimal_settings_s {
	SL_BASE_STRUCTURE_FIELDS;
	uint32_t optimalRenderWidth;
	uint32_t optimalRenderHeight;
	float optimalSharpness;
	uint32_t renderWidthMin;
	uint32_t renderHeightMin;
	uint32_t renderWidthMax;
	uint32_t renderHeightMax;
} sl_dlss_optimal_settings_t;

#endif // VK_DLSS_TYPES_H
