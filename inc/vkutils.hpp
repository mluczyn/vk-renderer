#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <vulkan/vulkan.hpp>

namespace vw {

namespace g {
extern vk::Device device;
extern vk::PhysicalDevice physicalDevice;
extern vk::Instance instance;
}  // namespace g

template <typename T>
class ArrayProxy {
 public:
  using ElementType = std::remove_reference_t<T>;
  using value_type = ElementType;
  ArrayProxy() : mPtr{nullptr}, mCount{0} {}
  ArrayProxy(const ElementType& ref) : mPtr{&ref}, mCount{1} {}
  template <uint32_t N>
  ArrayProxy(const T (&array)[N]) : mPtr{array}, mCount{N} {}
  template <size_t N>
  ArrayProxy(const std::array<ElementType, N>& array) : mPtr{array.data()}, mCount{static_cast<uint32_t>(array.size())} {}
  ArrayProxy(const std::vector<std::remove_const_t<ElementType>>& vector) : mPtr{vector.data()}, mCount{static_cast<uint32_t>(vector.size())} {}
  ArrayProxy(const std::initializer_list<ElementType>& iList) : mPtr{iList.begin()}, mCount{static_cast<uint32_t>(iList.size())} {}
  ArrayProxy(const T* ptr, uint32_t count) : mPtr{ptr}, mCount{count} {}
  T const* data() const {
    return mPtr;
  }
  const T& operator[](size_t i) const {
    return mPtr[i];
  }
  T const* begin() const {
    return mPtr;
  }
  T const* end() const {
    return mPtr + mCount;
  }
  const uint32_t size() const {
    return mCount;
  }
  const uint32_t byteSize() const {
    return mCount * sizeof(ElementType);
  }
  std::vector<T> copyToVec() const {
    return std::vector<T>{mPtr, mPtr + mCount};
  }

 private:
  T const* mPtr;
  const uint32_t mCount;
};

template <typename T>
class FixedVec {
 public:
  FixedVec(size_t capacity) {
    mBegin = alloc(capacity * sizeof(T));
    mEnd = mBegin;
    mCapacity = mBegin + capacity;
  }
  ~FixedVec() {
    dealloc(mBegin);
  }
  T* begin() {
    return mBegin;
  }
  const T* begin() const {
    return mBegin;
  }
  const T* cbegin() const {
    return mBegin;
  }
  T* end() {
    return mEnd;
  }
  const T* end() const {
    return mEnd;
  }
  const T* cend() const {
    return mEnd;
  }
  uint32_t size() const {
    return static_cast<uint32_t>(mEnd - mBegin);
  }
  uint32_t byteSize() const {
    return sizeof(T) * size();
  }
  uint32_t capacity() const {
    return static_cast<uint32_t>(mCapacity - mBegin);
  }
  T& operator[](size_t n) {
    return mBegin[n];
  }
  const T& operator[](size_t n) const {
    return mBegin[n];
  }
  template <typename... Args>
  T& emplace_back(Args&&... args) {
    new (mEnd) T(std::forward<Args>(args)...);
    mEnd++;
    return *mEnd;
  }

 private:
  static T* alloc(size_t byteCount) {
    if (byteCount == 0)
      return nullptr;
    return reinterpret_cast<T*>(new char[byteCount]);
  }
  static void dealloc(T* ptr) {
    if (ptr != nullptr)
      delete[] reinterpret_cast<char*>(ptr);
  }
  T* mBegin = nullptr;
  T* mEnd = nullptr;
  T* mCapacity = nullptr;
};

template <typename T>
class HandleContainer {
 public:
  inline T getHandle() const {
    return mHandle;
  }
  inline operator T() const {
    return mHandle;
  }
  inline operator ArrayProxy<T>() const {
    return mHandle;
  }

 protected:
  T mHandle;
};

template <typename T>
class HandleContainerUnique : public HandleContainer<T> {
 public:
  using ContainerType = HandleContainerUnique<T>;
  HandleContainerUnique() {}
  HandleContainerUnique(const ContainerType&) = delete;
  HandleContainerUnique(ContainerType&& other) noexcept {
    this->mHandle = other.mHandle;
    other.mHandle = VK_NULL_HANDLE;
  }
  ContainerType& operator=(const ContainerType&) = delete;
  ContainerType& operator=(ContainerType&& other) noexcept {
    this->mHandle = other.mHandle;
    other.mHandle = VK_NULL_HANDLE;
    return *this;
  }
  ~HandleContainerUnique() {
    if (this->mHandle)
      vw::g::device.destroy(this->mHandle);
  }
};

template <typename T>
inline uint32_t size32(const std::vector<T>& vector) {
  return static_cast<uint32_t>(vector.size());
}

template <typename T>
inline const T* optPtr(const std::optional<T>& opt) {
  return opt ? &(opt.value()) : nullptr;
}

template <typename T>
inline vk::DeviceSize byteSize(const T& container) {
  return sizeof(typename T::value_type) * container.size();
}

template <size_t N>
inline std::array<vk::DeviceSize, N> getOffsets(const std::array<vk::DeviceSize, N>& sizes, vk::DeviceSize firstOffset = 0) {
  std::array<vk::DeviceSize, N> offsets{};
  offsets[0] = firstOffset;
  for (size_t i = 1; i < N; ++i)
    offsets[i] = offsets[i - 1] + sizes[i - 1];
  return offsets;
}

template <typename TupleT>
constexpr auto tupleToArray(TupleT&& tuple) {
  constexpr auto to_array = [](auto&&... e) {
    return std::array{std::forward<decltype(e)>(e)...};
  };
  return std::apply(to_array, std::forward<TupleT>(tuple));
}

class DataFile {
 public:
  virtual ~DataFile() = default;
  virtual size_t dataSize() const = 0;
  virtual void loadData(std::byte* dst) const = 0;
};

class ImageFile : public DataFile {
 public:
  virtual ~ImageFile() = default;
  virtual vk::Format getFormat() const {
    return vk::Format::eUndefined;
  }
  virtual vk::Extent3D getExtent() const = 0;
};

template <typename T>
std::vector<T> loadBinaryFile(const std::filesystem::path path) {
  if (!std::filesystem::exists(path))
    throw std::runtime_error("File " + path.string() + " does not exist!");
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("File " + path.string() + " is not a regular file!");

  auto fSize = std::filesystem::file_size(path);
  if (fSize % sizeof(T) != 0)
    throw std::runtime_error("File " + path.string() + " size is not a multiple of sizeof(T) ");

  std::vector<T> dataVec(fSize / sizeof(T));
  {
    std::ifstream binaryFile{path, std::ios::binary};
    if (!binaryFile.read(reinterpret_cast<char*>(dataVec.data()), fSize))
      throw std::runtime_error("Error reading  file " + path.string());
  }
  return dataVec;
}

inline constexpr void alignTo(vk::DeviceSize& size, vk::DeviceSize alignment) {
  auto rem = size % alignment;
  if (rem != 0)
    size += alignment - rem;
}

}  // namespace vw