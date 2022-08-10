#pragma once
#include <array>
#include <cinttypes>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vulkan/vulkan.hpp>
#include "vkutils.hpp"

namespace vw {
namespace dds {
using DWORD = uint32_t;
DWORD toDWORD(const std::byte* bytes) {
  return *reinterpret_cast<const DWORD*>(bytes);
}
constexpr DWORD toDWORD(const char* chars) {
  return static_cast<DWORD>(static_cast<DWORD>(chars[0]) | (static_cast<DWORD>(chars[1]) << 8u) | (static_cast<DWORD>(chars[2]) << 16u) |
                            (static_cast<DWORD>(chars[3]) << 24u));
}

struct alignas(DWORD) PixelFormat {
  enum : DWORD { AlphaPixels = 0x1, Alpha = 0x2, FourCC = 0x4, RGB = 0x40, YUV = 0x200, Luminance = 0x20000 };
  enum CC : DWORD {
    DXT1 = toDWORD("DXT1"),
    DXT2 = toDWORD("DXT2"),
    DXT3 = toDWORD("DXT3"),
    DXT4 = toDWORD("DXT4"),
    DXT5 = toDWORD("DXT5"),
    DX10 = toDWORD("DX10"),
    ATI2 = toDWORD("ATI2")
  };
  DWORD size;
  DWORD flags;
  DWORD fourCC;
  DWORD rgbBitCount;
  DWORD bitMask[4];
  bool verify() const {
    return size == sizeof(PixelFormat);
  }
};
static_assert(sizeof(PixelFormat) == 32);

struct alignas(DWORD) Header {
  enum : DWORD {
    Caps = 0x1,
    Height = 0x2,
    Width = 0x4,
    Pitch = 0x8,
    PixelFormat = 0x1000,
    MipMapCount = 0x20000,
    LinearSize = 0x80000,
    Depth = 0x800000,
    Texture = Caps | Height | Width | PixelFormat
  };
  DWORD size;
  DWORD flags;
  DWORD height;
  DWORD width;
  DWORD pitchOrLinearSize;
  DWORD depth;
  DWORD mipMapCount;
  DWORD reserved1[11];
  dds::PixelFormat pixelFormat;
  DWORD caps[4];
  DWORD reserved2;
  bool verify() const {
    return (size == sizeof(Header)) && pixelFormat.verify();
  }
};
static_assert(sizeof(Header) == 124);

struct alignas(DWORD) FileStart {
  static constexpr DWORD kMagic = 0x20534444;
  DWORD magic;
  Header header;
  static FileStart fromBytes(const std::byte* bytes) {
    return *reinterpret_cast<const FileStart*>(bytes);
  }
  bool verify() const {
    return (magic == kMagic) && header.verify();
  }
};

enum class Format : DWORD {
  UNKNOWN,
  R32G32B32A32_TYPELESS,
  R32G32B32A32_FLOAT,
  R32G32B32A32_UINT,
  R32G32B32A32_SINT,
  R32G32B32_TYPELESS,
  R32G32B32_FLOAT,
  R32G32B32_UINT,
  R32G32B32_SINT,
  R16G16B16A16_TYPELESS,
  R16G16B16A16_FLOAT,
  R16G16B16A16_UNORM,
  R16G16B16A16_UINT,
  R16G16B16A16_SNORM,
  R16G16B16A16_SINT,
  R32G32_TYPELESS,
  R32G32_FLOAT,
  R32G32_UINT,
  R32G32_SINT,
  R32G8X24_TYPELESS,
  D32_FLOAT_S8X24_UINT,
  R32_FLOAT_X8X24_TYPELESS,
  X32_TYPELESS_G8X24_UINT,
  R10G10B10A2_TYPELESS,
  R10G10B10A2_UNORM,
  R10G10B10A2_UINT,
  R11G11B10_FLOAT,
  R8G8B8A8_TYPELESS,
  R8G8B8A8_UNORM,
  R8G8B8A8_UNORM_SRGB,
  R8G8B8A8_UINT,
  R8G8B8A8_SNORM,
  R8G8B8A8_SINT,
  R16G16_TYPELESS,
  R16G16_FLOAT,
  R16G16_UNORM,
  R16G16_UINT,
  R16G16_SNORM,
  R16G16_SINT,
  R32_TYPELESS,
  D32_FLOAT,
  R32_FLOAT,
  R32_UINT,
  R32_SINT,
  R24G8_TYPELESS,
  D24_UNORM_S8_UINT,
  R24_UNORM_X8_TYPELESS,
  X24_TYPELESS_G8_UINT,
  R8G8_TYPELESS,
  R8G8_UNORM,
  R8G8_UINT,
  R8G8_SNORM,
  R8G8_SINT,
  R16_TYPELESS,
  R16_FLOAT,
  D16_UNORM,
  R16_UNORM,
  R16_UINT,
  R16_SNORM,
  R16_SINT,
  R8_TYPELESS,
  R8_UNORM,
  R8_UINT,
  R8_SNORM,
  R8_SINT,
  A8_UNORM,
  R1_UNORM,
  R9G9B9E5_SHAREDEXP,
  R8G8_B8G8_UNORM,
  G8R8_G8B8_UNORM,
  BC1_TYPELESS,
  BC1_UNORM,
  BC1_UNORM_SRGB,
  BC2_TYPELESS,
  BC2_UNORM,
  BC2_UNORM_SRGB,
  BC3_TYPELESS,
  BC3_UNORM,
  BC3_UNORM_SRGB,
  BC4_TYPELESS,
  BC4_UNORM,
  BC4_SNORM,
  BC5_TYPELESS,
  BC5_UNORM,
  BC5_SNORM,
  B5G6R5_UNORM,
  B5G5R5A1_UNORM,
  B8G8R8A8_UNORM,
  B8G8R8X8_UNORM,
  R10G10B10_XR_BIAS_A2_UNORM,
  B8G8R8A8_TYPELESS,
  B8G8R8A8_UNORM_SRGB,
  B8G8R8X8_TYPELESS,
  B8G8R8X8_UNORM_SRGB,
  BC6H_TYPELESS,
  BC6H_UF16,
  BC6H_SF16,
  BC7_TYPELESS,
  BC7_UNORM,
  BC7_UNORM_SRGB,
  AYUV,
  Y410,
  Y416,
  NV12,
  P010,
  P016,
  YUV420_OPAQUE,
  YUY2,
  Y210,
  Y216,
  NV11,
  AI44,
  IA44,
  P8,
  A8P8,
  B4G4R4A4_UNORM,
  P208,
  V208,
  V408,
  SAMPLER_FEEDBACK_MIN_MIP_OPAQUE,
  SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE,
  FORCE_UINT
};

constexpr vk::Format kDDSFormatToVkFormat[] = {
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eR32G32B32A32Sfloat,
    vk::Format::eR32G32B32A32Uint,
    vk::Format::eR32G32B32A32Sint,
    vk::Format::eUndefined,
    vk::Format::eR32G32B32Sfloat,
    vk::Format::eR32G32B32Uint,
    vk::Format::eR32G32B32Sint,
    vk::Format::eUndefined,
    vk::Format::eR16G16B16A16Sfloat,
    vk::Format::eR16G16B16A16Unorm,
    vk::Format::eR16G16B16A16Uint,
    vk::Format::eR16G16B16A16Snorm,
    vk::Format::eR16G16B16A16Sint,
    vk::Format::eUndefined,
    vk::Format::eR32G32Sfloat,
    vk::Format::eR32G32Uint,
    vk::Format::eR32G32Sint,
    vk::Format::eUndefined,
    vk::Format::eD32SfloatS8Uint,
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eA2R10G10B10UnormPack32,  // RGBA -> ARGB
    vk::Format::eA2R10G10B10UintPack32,   // RGBA -> ARGB
    vk::Format::eB10G11R11UfloatPack32,   // RGB -> BGR
    vk::Format::eUndefined,
    vk::Format::eR8G8B8A8Unorm,
    vk::Format::eR8G8B8A8Unorm,  // SRGB
    vk::Format::eR8G8B8A8Uint,
    vk::Format::eR8G8B8A8Snorm,
    vk::Format::eR8G8B8A8Sint,
    vk::Format::eUndefined,
    vk::Format::eR16G16Sfloat,
    vk::Format::eR16G16Unorm,
    vk::Format::eR16G16Uint,
    vk::Format::eR16G16Snorm,
    vk::Format::eR16G16Sint,
    vk::Format::eUndefined,
    vk::Format::eD32Sfloat,
    vk::Format::eR32Sfloat,
    vk::Format::eR32Uint,
    vk::Format::eR32Sint,
    vk::Format::eUndefined,
    vk::Format::eD24UnormS8Uint,
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eR8G8Unorm,
    vk::Format::eR8G8Uint,
    vk::Format::eR8G8Snorm,
    vk::Format::eR8G8Sint,
    vk::Format::eUndefined,
    vk::Format::eR16Sfloat,
    vk::Format::eD16Unorm,
    vk::Format::eR16Unorm,
    vk::Format::eR16Uint,
    vk::Format::eR16Snorm,
    vk::Format::eR16Sint,
    vk::Format::eUndefined,
    vk::Format::eR8Unorm,
    vk::Format::eR8Uint,
    vk::Format::eR8Snorm,
    vk::Format::eR8Sint,
    vk::Format::eR8Unorm,               // A -> R
    vk::Format::eUndefined,             // R1_UNORM
    vk::Format::eE5B9G9R9UfloatPack32,  // RGBE -> EBGR
    vk::Format::eG8B8G8R8422Unorm,      // RGBG -> GBGR
    vk::Format::eB8G8R8G8422Unorm,      // GRGB -> BGRG
    vk::Format::eUndefined,
    vk::Format::eBc1RgbaUnormBlock,
    vk::Format::eBc1RgbaSrgbBlock,
    vk::Format::eUndefined,
    vk::Format::eBc2UnormBlock,
    vk::Format::eBc2SrgbBlock,
    vk::Format::eUndefined,
    vk::Format::eBc3UnormBlock,
    vk::Format::eBc3SrgbBlock,
    vk::Format::eUndefined,
    vk::Format::eBc4UnormBlock,
    vk::Format::eBc4SnormBlock,
    vk::Format::eUndefined,
    vk::Format::eBc5UnormBlock,
    vk::Format::eBc5SnormBlock,
    vk::Format::eB5G6R5UnormPack16,
    vk::Format::eB5G5R5A1UnormPack16,
    vk::Format::eB8G8R8A8Unorm,
    vk::Format::eB8G8R8A8Unorm,  // X -> A
    vk::Format::eUndefined,      // R10G10B10_XR_BIAS_A2_UNORM
    vk::Format::eUndefined,
    vk::Format::eB8G8R8A8Srgb,
    vk::Format::eUndefined,
    vk::Format::eB8G8R8A8Srgb,  // X -> A
    vk::Format::eUndefined,
    vk::Format::eBc6HUfloatBlock,
    vk::Format::eBc6HSfloatBlock,
    vk::Format::eUndefined,
    vk::Format::eBc7UnormBlock,
    vk::Format::eBc7SrgbBlock,
    // YUV, YCbCr formats, etc.
};
enum class ResourceDimension : DWORD { UNKOWN, BUFFER, TEXTURE1D, TEXTURE2D, TEXTURE3D };

struct alignas(DWORD) HeaderDX10 {
  enum : DWORD {
    ResourceMiscTextureCube = 0x4,
  };
  enum class AlphaMode : DWORD { Unknown = 0x0, Straight = 0x1, Premultiplied = 0x2, Opaque = 0x3, Custom = 0x4 };
  Format format;
  ResourceDimension resourceDimension;
  DWORD miscFlag;
  DWORD arraySize;
  DWORD miscFlags2;
  static HeaderDX10 fromBytes(const std::byte* bytes) {
    return *reinterpret_cast<const HeaderDX10*>(bytes);
  }
};

class DDSFile : public vw::ImageFile {
 public:
  DDSFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
      throw std::runtime_error("DDS file " + path.string() + " does not exist!");
    if (!std::filesystem::is_regular_file(path))
      throw std::runtime_error("DDS file " + path.string() + " is not a regular file!");

    mPath = path;
    mFileSize = std::filesystem::file_size(path);
    if (mFileSize < sizeof(FileStart)) {
      throw std::runtime_error("Invalid dds file size: " + path.string());
    }

    std::ifstream file{path, std::ios::binary | std::ios::in};
    std::byte fileStartBytes[sizeof(FileStart)];
    if (!file || !file.read(reinterpret_cast<char*>(&fileStartBytes), sizeof(FileStart))) {
      throw std::runtime_error("Could not read dds header: " + path.string());
    }
    mFileStart = FileStart::fromBytes(fileStartBytes);
    if (!mFileStart.verify())
      throw std::runtime_error("Invalid dds file: " + path.string());

    auto& header = mFileStart.header;
    auto& pixelFormat = header.pixelFormat;
    mIsDX10 = (pixelFormat.flags & PixelFormat::FourCC) && (pixelFormat.fourCC == PixelFormat::CC::DX10);

    mDataSize = mFileSize - sizeof(FileStart);
    mDataStart = file.tellg();
    if (mIsDX10) {
      if (mFileSize < (sizeof(FileStart) + sizeof(HeaderDX10))) {
        throw std::runtime_error("Invalid DX10 dds file size: " + path.string());
      }
      mDataSize -= sizeof(HeaderDX10);
      std::byte headerDX10Bytes[sizeof(HeaderDX10)];
      if (file.read(reinterpret_cast<char*>(&headerDX10Bytes), sizeof(HeaderDX10))) {
        throw std::runtime_error("Could not read dds dx10 header: " + path.string());
      }
      mHeaderDX10 = HeaderDX10::fromBytes(headerDX10Bytes);
      mDataStart = file.tellg();
    }
  }
  size_t dataSize() const override {
    return mDataSize;
  }
  void loadData(std::byte* dst) const override {
    std::ifstream file{mPath, std::ios::binary | std::ios::in};
    file.seekg(mDataStart);
    file.read(reinterpret_cast<char*>(dst), mDataSize);
  }
  vk::Format getFormat() const override {
    if (mIsDX10) {
      return kDDSFormatToVkFormat[static_cast<size_t>(mHeaderDX10.format)];
    }
    if (mFileStart.header.pixelFormat.flags & PixelFormat::FourCC) {
      DWORD fourCC = mFileStart.header.pixelFormat.fourCC;
      if (fourCC == PixelFormat::CC::DXT1) {
        return vk::Format::eBc1RgbaUnormBlock;
      }
      if (fourCC == PixelFormat::CC::DXT3) {
        return vk::Format::eBc2UnormBlock;
      }
      if (fourCC == PixelFormat::CC::DXT5) {
        return vk::Format::eBc3UnormBlock;
      }
      if (fourCC == PixelFormat::CC::ATI2) {
        return vk::Format::eBc5UnormBlock;
      }
    }
    return vk::Format::eUndefined;
  }
  vk::Extent3D getExtent() const override {
    return {mFileStart.header.width, mFileStart.header.height, 1};
  }

 private:
  std::filesystem::path mPath;
  FileStart mFileStart = {};
  HeaderDX10 mHeaderDX10 = {};
  size_t mFileSize = 0;
  size_t mDataSize = 0;
  std::ifstream::pos_type mDataStart;
  bool mIsDX10 = false;
};
};  // namespace dds
}  // namespace vw