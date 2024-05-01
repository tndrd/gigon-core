
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include "AsioContext.hpp"
#include "PortableEndian.h"
#include "Vst2Effect.hpp"

#undef max  // I hate windows

#include <limits>

namespace GigOn {

namespace Helpers {

size_t AsioSample2VstFloat(const void* src, float* dst, ASIOSampleType type) {
  assert(src);
  assert(dst);

#define CASEGEN(asioType, width, endianness)                  \
  case asioType: {                                            \
    using hostType = int##width##_t;                          \
    hostType val = *reinterpret_cast<const hostType*>(src);   \
    val = endianness##e##width##toh(val);                     \
    *dst = float(val) / std::numeric_limits<hostType>::max(); \
    return sizeof(hostType);                                  \
  }

  switch (type) {
    CASEGEN(ASIOSTInt16LSB, 16, l);
    CASEGEN(ASIOSTInt16MSB, 16, b);
    CASEGEN(ASIOSTInt32LSB, 32, l);
    CASEGEN(ASIOSTInt32MSB, 32, b);

    default:
      throw Helpers::LabelException(
          "Asio2Vst conversion",
          ASIOSampleTypeToStr(type) + std::string{"is not supported yet"});
  }

#undef CASEGEN
}

size_t VstFloat2AsioSample(const float* src, void* dst, ASIOSampleType type) {
  assert(src);
  assert(dst);

#define CASEGEN(asioType, width, endianness)                       \
  case asioType: {                                                 \
    using hostType = int##width##_t;                               \
    hostType* dst = reinterpret_cast<hostType*>(dst);              \
    hostType sample = *src * std::numeric_limits<hostType>::max(); \
    *dst = hto##endianness##e##width(sample);                      \
    return sizeof(hostType);                                       \
  }

  switch (type) {
    CASEGEN(ASIOSTInt16LSB, 16, l);
    CASEGEN(ASIOSTInt16MSB, 16, b);
    CASEGEN(ASIOSTInt32LSB, 32, l);
    CASEGEN(ASIOSTInt32MSB, 32, b);

    default:
      throw Helpers::LabelException(
          "Vst2Asio conversion",
          ASIOSampleTypeToStr(type) + std::string{"is not supported yet"});
  }
}

#undef CASEGEN
}  // namespace Helpers

struct AsioVstPlug final {
 private:
  VstProcessBuffer Inputs{0, 0};
  VstProcessBuffer Outputs{0, 0};

 public:
  AsioVstPlug() = default;

  AsioVstPlug(const AsioVstPlug&) = delete;
  AsioVstPlug& operator=(const AsioVstPlug&) = delete;

  AsioVstPlug(AsioVstPlug&&) = default;
  AsioVstPlug& operator=(AsioVstPlug&&) = default;
  ~AsioVstPlug() = default;

 public:
  void Configure(size_t blockSize, size_t nInputs, size_t nOutputs) {
    Inputs = VstProcessBuffer(blockSize, nInputs);
    Outputs = VstProcessBuffer(blockSize, nOutputs);
  }

  void Asio2VstInput(long channel, void* buffer, ASIOSampleType type) {
    assert(buffer);
    assert(channel >= 0);

    float* dst = Inputs.GetBufferByChannel(channel);
    const uint8_t* src = reinterpret_cast<uint8_t*>(buffer);

    size_t sz = Inputs.GetBlockSize();
    for (size_t i = 0; i < sz; ++i)
      src += Helpers::AsioSample2VstFloat(src, dst + i, type);
  }

  void Vst2AsioOutput(long channel, void* buffer, ASIOSampleType type) const {
    assert(buffer);
    assert(channel >= 0);

    const float* src = Outputs.GetBufferByChannel(channel);
    uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);

    size_t sz = Outputs.GetBlockSize();
    for (size_t i = 0; i < sz; ++i)
      dst += Helpers::VstFloat2AsioSample(src + i, dst, type);
  }

  const VstProcessBuffer& GetVstInputs() { return Inputs; }
  VstProcessBuffer& GetVstOutputs() { return Outputs; }
};

}  // namespace GigOn