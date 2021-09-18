#pragma once
#include <optional>
#include <vulkan/vulkan.hpp>

namespace vw {

template <typename T>
class ArrayProxy {
 public:
  using ElementType = std::remove_reference_t<T>;
  using value_type = ElementType;
  ArrayProxy() : mPtr{nullptr}, mCount{0} {}
  ArrayProxy(const ElementType& ref) : mPtr{&ref}, mCount{1} {}
  template <size_t N>
  ArrayProxy(const std::array<ElementType, N>& array) : mPtr{array.data()}, mCount{static_cast<uint32_t>(array.size())} {}
  ArrayProxy(const std::vector<std::remove_const_t<ElementType>>& vector) : mPtr{vector.data()}, mCount{static_cast<uint32_t>(vector.size())} {}
  ArrayProxy(const std::initializer_list<ElementType>& iList) : mPtr{iList.begin()}, mCount{static_cast<uint32_t>(iList.size())} {}
  ArrayProxy(const T* ptr, uint32_t count) : mPtr{ptr}, mCount{count} {}
  inline T const* data() const {
    return mPtr;
  }
  inline const T& operator[](size_t i) const {
    return mPtr[i];
  }
  inline T const* begin() const {
    return mPtr;
  }
  inline T const* end() const {
    return mPtr + mCount;
  }
  inline const uint32_t size() const {
    return mCount;
  }
  inline const uint32_t byteSize() const {
    return mCount * sizeof(ElementType);
  }
  inline std::vector<T> copyToVec() const {
    return std::vector<T>{mPtr, mPtr + mCount};
  }

 private:
  T const* mPtr;
  const uint32_t mCount;
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
  HandleContainerUnique(vk::Device device) : mDeviceHandle{device} {}
  HandleContainerUnique(const ContainerType&) = delete;
  HandleContainerUnique(ContainerType&& other) noexcept {
    mDeviceHandle = other.mDeviceHandle;
    this->mHandle = other.mHandle;
    other.mHandle = VK_NULL_HANDLE;
    other.mDeviceHandle = VK_NULL_HANDLE;
  }
  ContainerType& operator=(const ContainerType&) = delete;
  ContainerType& operator=(ContainerType&& other) noexcept {
    mDeviceHandle = other.mDeviceHandle;
    this->mHandle = other.mHandle;
    other.mDeviceHandle = VK_NULL_HANDLE;
    other.mHandle = VK_NULL_HANDLE;
    return *this;
  }
  ~HandleContainerUnique() {
    if (mDeviceHandle && this->mHandle)
      mDeviceHandle.destroy(this->mHandle);
  }
 protected:
  vk::Device mDeviceHandle = VK_NULL_HANDLE;
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
}  // namespace vw