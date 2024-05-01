#include <errno.h>
#include <windows.h>

#include <cassert>
#include <clocale>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "Helpers.hpp"
#include "aeffectx.h"

namespace GigOn {

class VstProcessBuffer {
  size_t BlockSize = 0;

  // aka channels
  size_t BufferLen = 0;

  std::vector<float> Buffer{};
  std::vector<float*> Pointers{};

 public:
  VstProcessBuffer(size_t blockSize, size_t bufferLen);

  float** GetVstBuffers();
  const float* const* GetVstBuffers() const;
  size_t GetBlockSize() const;
  size_t GetBufferLen() const;
};

// Vst2 AEffect* wrapper
class Vst2Effect final {
  static constexpr auto Label = "Vst2.4 effect wrapper";
  static constexpr size_t InfoStringSize = 256;
  static constexpr auto MainEntryName = "VSTPluginMain";

  // Signature of "VstPluginMain" function
  using PluginEntryProc = AEffect* (*)(audioMasterCallback);

  struct EffectDeleter {
    void operator()(AEffect* effect);
  };

  struct EffectInfo {
    std::string Effect;
    std::string Vendor;
    std::string Product;
    size_t NumInputs = 0;
    size_t NumOutputs = 0;
  } Info;

  Helpers::Flag Configured{false};
  Helpers::Flag Started{false};

  size_t BlockSize = 0;
  std::unique_ptr<AEffect, EffectDeleter> Effect{};

 public:
  Vst2Effect(const Helpers::DllLoader& dll);
  void Configure(float sampleRate, VstInt32 blockSize);

  void Start();
  void Stop();

  void Process(VstProcessBuffer& input, VstProcessBuffer& output);
  EffectInfo GetInfo() const;

  Vst2Effect(const Vst2Effect&) = delete;
  Vst2Effect& operator=(const Vst2Effect&) = delete;

  Vst2Effect(Vst2Effect&&) = default;
  Vst2Effect& operator=(Vst2Effect&&) = default;

  ~Vst2Effect() = default;

 private:
  // Binding for the dispatcher
  VstIntPtr Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value,
                       void* ptr, float opt);

  void OpenImpl();

  // It has to have this kind of interface
  // to be accesed from deleter
  static void CloseImpl(AEffect* effect);

  void SetSampleRateImpl(float rate);
  void SetBlockSizeImpl(VstInt32 size);

  void StartImpl();
  void StopImpl();

  void FetchInfoString(VstInt32 opCode, std::string& dest);

  void FetchInfo();

  // Audiomaster callback that handles plugin queries
  static VstIntPtr VSTCALLBACK AMCallback(AEffect* effect, VstInt32 opCode,
                                          VstInt32 index, VstIntPtr value,
                                          void* ptr, float opt);
};

}  // namespace GigOn