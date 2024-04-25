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

#include "aeffectx.h"

/*** Some compile-time checks ***/

// NOLINTBEGIN
#if VST_64BIT_PLATFORM
#pragma message("*** VST: Building for 64-bit platform ***\n")
#else
#pragma message("*** VST: Building for 32-bit platform ***\n")
#endif
// NOLINTEND

#define ASSERT_SIZE(type, size) \
  static_assert(sizeof(type) == size, #type ": invalid size");

ASSERT_SIZE(VstInt16, 2);
ASSERT_SIZE(VstInt32, 4);
ASSERT_SIZE(VstInt64, 8);
ASSERT_SIZE(VstIntPtr, sizeof(void*));

#undef ASSERT_SIZE

/*** End of complile-time checks ***/

namespace GigOn {

namespace Helpers {
std::string WinErrToStr(DWORD err) {
  return std::system_category().message(err);
}

struct LabelException : public std::runtime_error {
  LabelException(const std::string& label, const std::string& msg)
      : std::runtime_error{label + ": " + msg} {}
  virtual ~LabelException() {}
};

struct WinException : public LabelException {
  static constexpr auto Label = "Windows error";
  WinException(const std::string& msg, DWORD err)
      : LabelException{Label, msg + ": " + Helpers::WinErrToStr(err)} {}

  virtual ~WinException() {}
};

}  // namespace Helpers

// RAII DLL Handle
class DllLoader final {
  static constexpr auto Label = "Dll loader";

  using ModuleVal = HINSTANCE__;  // aka decltype(*HMODULE)

  struct ModuleDeleter final {
    void operator()(ModuleVal* ptr) const {
      if (ptr) FreeLibrary(ptr);
    }
  };

  std::unique_ptr<ModuleVal, ModuleDeleter> Module;
  std::string Path;

 public:
  // We will delegate nothrow file existence check to user :)
  DllLoader(const std::string& path) : Path{path} {
    auto module = LoadLibraryA(path.c_str());

    if (!module)
      throw Helpers::WinException("Failed to load module at \"" + path + "\"",
                                  GetLastError());

    Module = decltype(Module){module};
  }

  DllLoader(const DllLoader&) = delete;
  DllLoader& operator=(const DllLoader&) = delete;

  DllLoader& operator=(DllLoader&&) = default;
  DllLoader(DllLoader&&) = default;

  ~DllLoader() = default;

 public:
  FARPROC GetProcAddress(const std::string& procName) const {
    if (!Module) throw Helpers::LabelException(Label, "Dll not loaded");

    FARPROC proc = ::GetProcAddress(Module.get(), procName.c_str());
    if (proc) return proc;

    throw Helpers::WinException("Failed to get procedure \"" + procName + "\"",
                                GetLastError());
  }

  const std::string& GetPath() const { return Path; }
};

class VstProcessBuffer {
  size_t BlockSize = 0;

  // aka channels
  size_t BufferLen = 0;

  std::vector<float> Buffer{};
  std::vector<float*> Pointers{};

 public:
  VstProcessBuffer(size_t blockSize, size_t bufferLen)
      : BlockSize{blockSize}, BufferLen{bufferLen} {
    Buffer = std::vector<float>(bufferLen * blockSize, 0);
    Pointers = std::vector<float*>(bufferLen, 0);

    for (int i = 0; i < bufferLen; ++i) Pointers[i] = &Buffer[blockSize * i];
  }

  float** GetVstBuffers() { return Pointers.data(); }
  const float* const* GetVstBuffers() const { return Pointers.data(); }

  size_t GetBlockSize() const { return BlockSize; }

  size_t GetBufferLen() const { return BufferLen; }
};

// Vst2 AEffect* wrapper
class Vst2Effect final {
  static constexpr auto Label = "Vst2.4 effect wrapper";
  static constexpr size_t InfoStringSize = 256;
  static constexpr auto MainEntryName = "VSTPluginMain";

  // Signature of "VstPluginMain" function
  using PluginEntryProc = AEffect* (*)(audioMasterCallback);

  struct EffectDeleter {
    void operator()(AEffect* effect) { CloseImpl(effect); }
  };

  struct Flag {
   private:
    bool Value;

   public:
    explicit Flag(bool val) : Value{val} {}
    Flag(const Flag&) = delete;
    Flag& operator=(const Flag&) = delete;

    Flag(Flag&& rhs) : Value{rhs.Value} {}
    Flag& operator=(Flag&& rhs) {
      std::swap(Value, rhs.Value);
      return *this;
    }

    bool& Access() { return Value; }
  };

  struct EffectInfo {
    std::string Effect;
    std::string Vendor;
    std::string Product;
    size_t NumInputs = 0;
    size_t NumOutputs = 0;
  } Info;

  Flag Configured{false};
  Flag Started{false};

  size_t BlockSize = 0;
  std::unique_ptr<AEffect, EffectDeleter> Effect{};

 public:
  Vst2Effect(const DllLoader& dll) {
    FARPROC proc = dll.GetProcAddress(MainEntryName);
    auto entry = reinterpret_cast<PluginEntryProc>(proc);

    AEffect* newEffect = entry(AMCallback);

    if (!newEffect)
      throw Helpers::LabelException(
          Label, "Failed to load plugin from " + dll.GetPath());

    Effect = {newEffect, {}};
    OpenImpl();
    FetchInfo();
  }

  void Configure(float sampleRate, VstInt32 blockSize) {
    if (Started.Access())
      throw Helpers::LabelException(Label, "Can't configure: now running");

    SetSampleRateImpl(sampleRate);
    SetBlockSizeImpl(blockSize);

    BlockSize = blockSize;
    Configured.Access() = true;
  }

  void Start() {
    if (!Configured.Access())
      throw Helpers::LabelException(Label, "Can't start: not configured");
    if (Started.Access())
      throw Helpers::LabelException(Label, "Cant't start: already started");

    StartImpl();
    Started.Access() = true;
  }

  void Stop() {
    if (!Started.Access())
      throw Helpers::LabelException(Label, "Can't stop: not running");

    StopImpl();
    Started.Access() = false;
  }

  void Process(VstProcessBuffer& input, VstProcessBuffer& output) {
    if (!Started.Access())
      throw Helpers::LabelException(Label, "Can't process: not running");

    if (input.GetBlockSize() != BlockSize ||
        input.GetBufferLen() != Effect->numInputs)
      throw Helpers::LabelException(Label,
                                    "Can't process: incorrect input buffers");

    if (output.GetBlockSize() != BlockSize ||
        output.GetBufferLen() != Effect->numOutputs)
      throw Helpers::LabelException(Label,
                                    "Can't process: incorrect output buffers");

    Effect->processReplacing(Effect.get(), input.GetVstBuffers(),
                             output.GetVstBuffers(), BlockSize);
  }

  EffectInfo GetInfo() const { return Info; }

  Vst2Effect(const Vst2Effect&) = delete;
  Vst2Effect& operator=(const Vst2Effect&) = delete;

  Vst2Effect(Vst2Effect&&) = default;
  Vst2Effect& operator=(Vst2Effect&&) = default;

  ~Vst2Effect() = default;

 private:
  // Binding for the dispatcher
  VstIntPtr Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value,
                       void* ptr, float opt) {
    assert(Effect);
    return Effect->dispatcher(Effect.get(), opCode, index, value, ptr, opt);
  }

  void OpenImpl() { Dispatcher(effOpen, 0, 0, 0, 0); }

  // It has to have this kind of interface
  // to be accesed from deleter
  static void CloseImpl(AEffect* effect) {
    assert(effect);
    effect->dispatcher(effect, effClose, 0, 0, 0, 0);
  }

  void SetSampleRateImpl(float rate) {
    Dispatcher(effSetSampleRate, 0, 0, 0, rate);
  }
  void SetBlockSizeImpl(VstInt32 size) {
    Dispatcher(effSetBlockSize, 0, size, 0, 0);
  }

  void StartImpl() { Dispatcher(effMainsChanged, 0, 1, 0, 0); }
  void StopImpl() { Dispatcher(effMainsChanged, 0, 0, 0, 0); }

  void FetchInfoString(VstInt32 opCode, std::string& dest) {
    dest = std::string(InfoStringSize, 0);
    Dispatcher(opCode, 0, 0, &dest[0], 0);
  }

  void FetchInfo() {
    FetchInfoString(effGetEffectName, Info.Effect);
    FetchInfoString(effGetVendorString, Info.Vendor);
    FetchInfoString(effGetProductString, Info.Product);

    Info.NumInputs = Effect->numInputs;
    Info.NumOutputs = Effect->numOutputs;
  }

  // Audiomaster callback that handles plugin queries
  static VstIntPtr VSTCALLBACK AMCallback(AEffect* effect, VstInt32 opCode,
                                          VstInt32 index, VstIntPtr value,
                                          void* ptr, float opt) {
    VstIntPtr result = 0;

    switch (opCode) {
      case audioMasterIdle:
        break;
      case audioMasterGetCurrentProcessLevel:
        result = kVstProcessLevelRealtime;
        break;
      case audioMasterVersion:
        result = kVstVersion;
        break;
    }

    return result;
  }
};

}  // namespace GigOn

int main() try {
#define TAB "  "

  setlocale(LC_ALL, "");

  const char* path = "Parallax.dll";

  auto loader = GigOn::DllLoader(path);
  auto effect = GigOn::Vst2Effect(loader);

  auto info = effect.GetInfo();

  std::cout << "*** VST2 Plugin Info ***" << std::endl;
  std::cout << TAB "Name:    " << info.Effect << std::endl;
  std::cout << TAB "Vendor:  " << info.Vendor << std::endl;
  std::cout << TAB "Product: " << info.Product << std::endl;
  std::cout << TAB "Inputs:  " << info.NumInputs << std::endl;
  std::cout << TAB "Outputs: " << info.NumOutputs << std::endl;

  effect.Configure(48000.f, 64);
  effect.Start();

  GigOn::VstProcessBuffer input(64, 2);
  GigOn::VstProcessBuffer output(64, 2);

  effect.Process(input, output);

  effect.Stop();

} catch (std::exception& e) {
  std::cout << e.what() << std::endl;
}