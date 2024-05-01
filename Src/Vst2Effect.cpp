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

VstProcessBuffer::VstProcessBuffer(size_t blockSize, size_t nChannels)
    : BlockSize{blockSize}, NChannels{nChannels} {
  Buffer = std::vector<float>(nChannels * blockSize, 0);
  Pointers = std::vector<float*>(nChannels, 0);

  for (int i = 0; i < nChannels; ++i) Pointers[i] = &Buffer[blockSize * i];
}

auto VstProcessBuffer::GetVstBuffers() -> VstBufferT { return Pointers.data(); }
auto VstProcessBuffer::GetVstBuffers() const -> CVstBufferT {
  return Pointers.data();
}

float* VstProcessBuffer::GetBufferByChannel(size_t channel) {
  assert(channel < NChannels.Access());
  return Pointers[channel];
}

const float* VstProcessBuffer::GetBufferByChannel(size_t channel) const {
  assert(channel < NChannels.Access());
  return Pointers[channel];
}

size_t VstProcessBuffer::GetBlockSize() const { return BlockSize.Access(); }
size_t VstProcessBuffer::GetChannels() const { return NChannels.Access(); }

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

void Vst2Effect::Process(const VstProcessBuffer& input,
                         VstProcessBuffer& output) {
  if (!Started.Access())
    throw Helpers::LabelException(Label, "Can't process: not running");

  if (input.GetBlockSize() != BlockSize ||
      input.GetChannels() != Effect->numInputs)
    throw Helpers::LabelException(Label,
                                  "Can't process: incorrect input buffers");

  if (output.GetBlockSize() != BlockSize ||
      output.GetChannels() != Effect->numOutputs)
    throw Helpers::LabelException(Label,
                                  "Can't process: incorrect output buffers");

  // For some reason an API accepts non-const pointer to input buffer
  // So we have to cast it here
  float** inputBuf = const_cast<float**>(input.GetVstBuffers());
  float** outputBuf = output.GetVstBuffers();

  Effect->processReplacing(Effect.get(), inputBuf, outputBuf, BlockSize);
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