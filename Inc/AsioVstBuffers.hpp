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

size_t AsioSample2VstFloat(const void* src, float* dst, ASIOSampleType type);
size_t VstFloat2AsioSample(const float* src, void* dst, ASIOSampleType type);
}  // namespace Helpers
struct AsioVstBuffers final {
 private:
  VstProcessBuffer Inputs{0, 0};
  VstProcessBuffer Outputs{0, 0};

 public:
  AsioVstBuffers() = default;

  AsioVstBuffers(const AsioVstBuffers&) = delete;
  AsioVstBuffers& operator=(const AsioVstBuffers&) = delete;

  AsioVstBuffers(AsioVstBuffers&&) = default;
  AsioVstBuffers& operator=(AsioVstBuffers&&) = default;
  ~AsioVstBuffers() = default;

 public:
  void Configure(size_t blockSize, size_t nInputs, size_t nOutputs);
  void Asio2VstInput(long channel, void* buffer, ASIOSampleType type);

  void Vst2AsioOutput(long channel, void* buffer, ASIOSampleType type) const;
  const VstProcessBuffer& GetVstInputs();
  VstProcessBuffer& GetVstOutputs();
};

}  // namespace GigOn