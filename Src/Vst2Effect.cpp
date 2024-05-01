#include "Vst2Effect.hpp"

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

VstProcessBuffer::VstProcessBuffer(size_t blockSize, size_t bufferLen)
    : BlockSize{blockSize}, BufferLen{bufferLen} {
  Buffer = std::vector<float>(bufferLen * blockSize, 0);
  Pointers = std::vector<float*>(bufferLen, 0);

  for (int i = 0; i < bufferLen; ++i) Pointers[i] = &Buffer[blockSize * i];
}

float** VstProcessBuffer::GetVstBuffers() { return Pointers.data(); }
const float* const* VstProcessBuffer::GetVstBuffers() const {
  return Pointers.data();
}

size_t VstProcessBuffer::GetBlockSize() const { return BlockSize; }

size_t VstProcessBuffer::GetBufferLen() const { return BufferLen; }

Vst2Effect::Vst2Effect(const Helpers::DllLoader& dll) {
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

void Vst2Effect::Configure(float sampleRate, VstInt32 blockSize) {
  if (Started.Access())
    throw Helpers::LabelException(Label, "Can't configure: now running");

  SetSampleRateImpl(sampleRate);
  SetBlockSizeImpl(blockSize);

  BlockSize = blockSize;
  Configured.Access() = true;
}

void Vst2Effect::Start() {
  if (!Configured.Access())
    throw Helpers::LabelException(Label, "Can't start: not configured");
  if (Started.Access())
    throw Helpers::LabelException(Label, "Cant't start: already started");

  StartImpl();
  Started.Access() = true;
}

void Vst2Effect::Stop() {
  if (!Started.Access())
    throw Helpers::LabelException(Label, "Can't stop: not running");

  StopImpl();
  Started.Access() = false;
}

void Vst2Effect::Process(VstProcessBuffer& input, VstProcessBuffer& output) {
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

auto Vst2Effect::GetInfo() const -> EffectInfo { return Info; }

auto Vst2Effect::Dispatcher(VstInt32 opCode, VstInt32 index, VstIntPtr value,
                            void* ptr, float opt) -> VstIntPtr {
  assert(Effect);
  return Effect->dispatcher(Effect.get(), opCode, index, value, ptr, opt);
}

void Vst2Effect::OpenImpl() { Dispatcher(effOpen, 0, 0, 0, 0); }

void Vst2Effect::CloseImpl(AEffect* effect) {
  assert(effect);
  effect->dispatcher(effect, effClose, 0, 0, 0, 0);
}

void Vst2Effect::EffectDeleter::operator()(AEffect* effect) {
  CloseImpl(effect);
}

void Vst2Effect::SetSampleRateImpl(float rate) {
  Dispatcher(effSetSampleRate, 0, 0, 0, rate);
}
void Vst2Effect::SetBlockSizeImpl(VstInt32 size) {
  Dispatcher(effSetBlockSize, 0, size, 0, 0);
}

void Vst2Effect::StartImpl() { Dispatcher(effMainsChanged, 0, 1, 0, 0); }
void Vst2Effect::StopImpl() { Dispatcher(effMainsChanged, 0, 0, 0, 0); }

void Vst2Effect::FetchInfoString(VstInt32 opCode, std::string& dest) {
  dest = std::string(InfoStringSize, 0);
  Dispatcher(opCode, 0, 0, &dest[0], 0);
}

void Vst2Effect::FetchInfo() {
  FetchInfoString(effGetEffectName, Info.Effect);
  FetchInfoString(effGetVendorString, Info.Vendor);
  FetchInfoString(effGetProductString, Info.Product);

  Info.NumInputs = Effect->numInputs;
  Info.NumOutputs = Effect->numOutputs;
}

// Audiomaster callback that handles plugin queries
VstIntPtr VSTCALLBACK Vst2Effect::AMCallback(AEffect* effect, VstInt32 opCode,
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

}  // namespace GigOn

int main() try {
#define TAB "  "

  setlocale(LC_ALL, "");

  const char* path = "Parallax.dll";

  auto loader = GigOn::Helpers::DllLoader(path);
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