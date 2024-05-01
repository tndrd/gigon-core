#include "AsioContext.hpp"

#include <cassert>

#undef min
#undef max

#define TAB "   "
#define LINE "--------------------------------------"

namespace GigOn {

const size_t ASIO_DRIVER_NAME_LEN = 32;

AsioContext::Exception::Exception(const std::string& msg, ASIOError errorCode)
    : std::runtime_error{msg + ": " + Helpers::ASIOErrorToStr(errorCode)} {}
AsioContext::Exception::~Exception() {}

AsioDrivers& AsioContext::GetAsioDrivers() {
  static AsioDrivers drivers{};
  return drivers;
}

AsioContext::AsioContext() {
  AsioCallbacks.asioMessage = AsioMessageCallback;
  AsioCallbacks.bufferSwitch = AsioBufferSwitchCallback;
  AsioCallbacks.bufferSwitchTimeInfo = AsioBufferSwitchTimeInfoCallback;
  AsioCallbacks.sampleRateDidChange = AsioSampleRateChangedCallback;
}

AsioContext& AsioContext::Get() {
  static AsioContext ctx;
  return ctx;
}

void AsioContext::LoadDriver(const std::string& driverName) {
  Expect(!Loaded, Msg::AlreadyLoaded);

  std::string tmp = driverName;

  if (!GetAsioDrivers().loadDriver(&tmp[0]))
    throw std::runtime_error("Failed to load driver \"" + driverName + "\"");

  Loaded = true;
}

void AsioContext::InitDriver() {
  Expect(Loaded, Msg::NotLoaded);
  Expect(!Initialized, Msg::AlreadyLoaded);

  ASIOError status = ASIOInit(&AsioInfo);
  if (status != ASE_OK) throw Exception("Failed to init driver", status);

  DeviceInfo = GetDeviceInfoInternal();
  PostOutput = ASIOOutputReady() == ASE_OK;

  Initialized = true;
}

void AsioContext::SetHandlers(ProcessorT&& proc, HandlerT&& handler) {
  Expect(Initialized, Msg::NotInit);
  Expect(!BuffersCreated, Msg::BuffersPresent);

  Processor = std::move(proc);
  Handler = std::move(handler);

  HandlersSet = true;
}

void AsioContext::CreateBuffers(const std::vector<ChannelId>& inputs,
                                const std::vector<ChannelId>& outputs,
                                size_t bufferSize) {
  Expect(HandlersSet, Msg::NoHandlersSet);
  Expect(!BuffersCreated, Msg::BuffersPresent);

  CheckBufferSize(bufferSize);

  std::vector<ASIOBufferInfo> binfos(inputs.size() + outputs.size(),
                                     ASIOBufferInfo{});

  int i = 0;

  for (; i < inputs.size(); ++i) {
    binfos[i].isInput = ASIOTrue;
    binfos[i].channelNum = inputs[i];
    binfos[i].buffers[0] = 0;
    binfos[i].buffers[1] = 0;
  }

  for (; i < inputs.size() + outputs.size(); ++i) {
    binfos[i].isInput = ASIOFalse;
    binfos[i].channelNum = outputs[i - inputs.size()];
    binfos[i].buffers[0] = 0;
    binfos[i].buffers[1] = 0;
  }

  ASIOError status = ASIOCreateBuffers(binfos.data(), binfos.size(), bufferSize,
                                       &AsioCallbacks);
  if (status != ASE_OK) throw Exception("ASIOCreateBuffers()", status);

  // Kalb line here

  ActiveBuffersInfo.NumInput = inputs.size();
  ActiveBuffersInfo.NumOutput = outputs.size();
  ActiveBuffersInfo.BufferSize = bufferSize;

  AsioBufferInfos = std::move(binfos);

  Processor->Configure(bufferSize, inputs.size(), outputs.size());

  BuffersCreated = true;
}

void AsioContext::Start() {
  Expect(BuffersCreated, Msg::BuffersAbsent);
  Expect(!Started, Msg::AlreadyRunning);

  ASIOError status = ASIOStart();
  if (status != ASE_OK) throw Exception("ASIOStart()", status);

  Started = true;
}

void AsioContext::Stop() {
  Expect(Started, Msg::NotRunning);

  ASIOError status = ASIOStop();
  if (status != ASE_OK) throw Exception("ASIOStop()", status);

  Started = false;
}

void AsioContext::DisposeBuffers() {
  Expect(BuffersCreated, Msg::BuffersAbsent);
  Expect(!Started, Msg::AlreadyRunning);

  ASIOError status = ASIODisposeBuffers();
  if (status != ASE_OK) throw Exception("ASIODisposeBuffers()", status);

  BuffersCreated = false;
}

void AsioContext::DeInitDriver() {
  Expect(Initialized, Msg::NotInit);
  Expect(!BuffersCreated, Msg::BuffersPresent);

  ASIOError status = ASIOExit();
  if (status != ASE_OK) throw Exception("ASIOExit()", status);

  Initialized = false;
}

void AsioContext::UnloadDriver() {
  Expect(Loaded, Msg::NotLoaded);
  Expect(!Initialized, Msg::AlreadyInit);

  GetAsioDrivers().removeCurrentDriver();
  Loaded = false;
}

auto AsioContext::GetDeviceInfo() const -> DeviceInformation {
  Expect(Initialized, Msg::NotInit);

  return DeviceInfo;
}

ASIODriverInfo AsioContext::GetAsioInfo() const {
  Expect(Initialized, Msg::NotInit);

  return AsioInfo;
}

auto AsioContext::GetBuffersInfo() const -> BuffersInformation {
  Expect(BuffersCreated, Msg::BuffersAbsent);

  return ActiveBuffersInfo;
}

void AsioContext::Expect(bool var, const char* msg) {
  if (!var) throw std::runtime_error(msg);
}

auto AsioContext::GetDeviceInfoInternal() const -> DeviceInformation {
  assert(Loaded);

  ASIOError status;
  DeviceInformation info;

  long numInputs = info.NumChannels.Input;
  long numOutputs = info.NumChannels.Output;
  long minSize = info.BufferInfo.MinSize;
  long maxSize = info.BufferInfo.MaxSize;
  long prefSize = info.BufferInfo.PrefSize;
  long granularity = info.BufferInfo.Granularity;

  if ((status = ASIOGetChannels(&numInputs, &numOutputs)) != ASE_OK)
    throw Exception("ASIOGetChannels", status);

  numInputs = numInputs;
  numOutputs = numOutputs;

  for (size_t i = 0; i < numInputs + numOutputs; ++i) {
    ASIOChannelInfo chinfo;
    chinfo.isInput = i < numInputs;
    chinfo.channel = chinfo.isInput ? i : i - numInputs;

    if ((status = ASIOGetChannelInfo(&chinfo)) != ASE_OK)
      throw Exception("ASIOGetChannelInfo()", status);

    if (chinfo.isInput) {
      info.Inputs.push_back(chinfo);
    } else {
      info.Outputs.push_back(chinfo);
    }
  }

  if ((status = ASIOGetBufferSize(&minSize, &maxSize, &prefSize,
                                  &granularity)) != ASE_OK)
    throw Exception("AsioGetBufferSize()", status);

  if ((status = ASIOGetSampleRate(&info.SampleRate)) != ASE_OK)
    throw Exception("ASIOGetSampleRate()", status);

  // Idk why ASIO SDK uses a signed type for channel ID's
  // I'd like to use the unsigned ones, so the conversion
  // has to be done somewhere. It seems that SDK will never return
  // a negative value in these cases, so we don't lose any information.
  // Guess I'll put some asserts here just in case

  assert(numInputs >= 0);
  assert(numOutputs >= 0);
  assert(minSize >= 0);
  assert(maxSize >= 0);
  assert(prefSize >= 0);
  assert(granularity >= 0);

  info.NumChannels.Input = numInputs;
  info.NumChannels.Output = numOutputs;
  info.BufferInfo.MinSize = minSize;
  info.BufferInfo.MaxSize = maxSize;
  info.BufferInfo.PrefSize = prefSize;
  info.BufferInfo.Granularity = granularity;

  return info;
}

void AsioContext::CheckBufferSize(long bufSize) const {
  assert(Initialized);

  bool cond1 = bufSize < DeviceInfo.BufferInfo.MinSize;
  bool cond2 = bufSize > DeviceInfo.BufferInfo.MaxSize;
  bool cond3 = !!(bufSize % DeviceInfo.BufferInfo.Granularity);

  if (cond1 || cond2 || cond3)
    throw std::runtime_error("Incorrect buffer size");
}

void AsioContext::AsioBufferSwitchCallback(long index, ASIOBool processNow) {
  // Yup it's the only way. We can't provide
  // additional arguments to asio callbacks
  const auto& asio = AsioContext::Get();
  auto bufInfos = asio.GetBuffersInfo();

  for (int i = 0; i < bufInfos.NumInput + bufInfos.NumOutput; ++i) {
    void* bufPtr = asio.AsioBufferInfos[i].buffers[index];
    long channel = asio.AsioBufferInfos[i].channelNum;
    bool isInput = asio.AsioBufferInfos[i].isInput;

    const auto& channelInfo = isInput ? asio.DeviceInfo.Inputs[channel]
                                      : asio.DeviceInfo.Outputs[channel];

    if (isInput)
      asio.Processor->ProcessInput(channel, bufPtr, channelInfo.type);
    else
      asio.Processor->ProcessOutput(channel, bufPtr, channelInfo.type);
  }

  if (asio.PostOutput) ASIOOutputReady();
}

ASIOTime* AsioContext::AsioBufferSwitchTimeInfoCallback(ASIOTime* timeInfo,
                                                        long index,
                                                        ASIOBool processNow) {
  AsioBufferSwitchCallback(index, processNow);
  return nullptr;
}

void AsioContext::AsioSampleRateChangedCallback(ASIOSampleRate sRate) {
  assert(0 && "Not yet implemented");
}

long AsioContext::AsioMessageCallback(long selector, long value, void* message,
                                      double* opt) {
  long ret = 0;

  switch (selector) {
    case kAsioSelectorSupported:
      if (value == kAsioEngineVersion || kAsioResetRequest || kAsioOverload)
        ret = 1;
      break;
    case kAsioEngineVersion:
      ret = 2;
      break;
    case kAsioResetRequest:
      ret = 1;
      break;
    case kAsioOverload:
      AsioContext::Get().Handler->HandleEvent(
          AsioContext::DriverEvent::Overload);
      break;
  }

  return ret;
}

ASIOError AsioContext::DtorStopDriver() {
  if (!Started) return ASE_OK;
  return ASIOStop();
}

ASIOError AsioContext::DtorDisposeBuffers() {
  if (!BuffersCreated) return ASE_OK;
  return ASIODisposeBuffers();
}

ASIOError AsioContext::DtorExitDriver() {
  if (!Initialized) return ASE_OK;
  return ASIOExit();
}

AsioContext::~AsioContext() {
  ASIOError status = ASE_OK;

  if ((status = DtorStopDriver()) != ASE_OK)
    std::cout << "ASIOStop: " << Helpers::ASIOErrorToStr(status)
              << std::endl;  // TODO LOGGER

  if ((status = DtorDisposeBuffers()) != ASE_OK)
    std::cout << "ASIODisposeBuffers: " << Helpers::ASIOErrorToStr(status)
              << std::endl;  // TODO LOGGER

  if ((status = DtorExitDriver()) != ASE_OK)
    std::cout << "ASIOExit: " << Helpers::ASIOErrorToStr(status)
              << std::endl;  // TODO LOGGER

  GetAsioDrivers().removeCurrentDriver();
}

std::vector<std::string> AsioContext::GetDriverNames(size_t maxNames) {
  if (maxNames == 0) return {};

  std::vector<char> buffer(ASIO_DRIVER_NAME_LEN * maxNames, 0);
  std::vector<char*> pointers(maxNames, 0);

  for (int i = 0; i < maxNames; ++i) {
    pointers[i] = buffer.data() + i * ASIO_DRIVER_NAME_LEN;
  }

  auto drvAvailable =
      GetAsioDrivers().getDriverNames(pointers.data(), pointers.size());

  if (drvAvailable == 0) return {};

  std::vector<std::string> result;

  for (int i = 0; i < drvAvailable; ++i) result.emplace_back(pointers[i]);

  return result;
}

void Helpers::DumpAsioInfo(std::ostream& out, const ASIODriverInfo& info) {
  out << "ASIO Driver info dump: " << std::endl;
  out << TAB "Driver name:   " << info.name << std::endl;
  out << TAB "Error message: " << info.errorMessage << std::endl;
}

void Helpers::DumpDeviceInfo(std::ostream& out,
                             const AsioContext::DeviceInformation& info) {
  out << "Channels:" << std::endl;
  out << TAB "Inputs:  " << info.NumChannels.Input << std::endl;
  out << TAB "Outputs: " << info.NumChannels.Output << std::endl;

  for (int i = 0; i < info.Inputs.size() + info.Outputs.size(); ++i) {
    int index = 0;
    const decltype(info.Outputs)* contPtr = nullptr;
    if (i < info.Inputs.size()) {
      index = i;
      contPtr = &info.Inputs;
    } else {
      index = i - info.Inputs.size();
      contPtr = &info.Outputs;
    }

    const auto& chinfo = (*contPtr)[index];

    out << "ASIO Channel info dump: " << std::endl;
    out << TAB "Name:    " << chinfo.name << std::endl;
    out << TAB "Channel: " << chinfo.channel << std::endl;
    out << TAB "Type:    " << (chinfo.isInput ? "Input" : "Output")
        << std::endl;
    out << TAB "Group:   " << chinfo.channelGroup << std::endl;
    out << TAB "Active:  " << (chinfo.isActive ? "Yes" : "No") << std::endl;
    out << TAB "SplType: " << ASIOSampleTypeToStr(chinfo.type) << std::endl;
  }

  out << "Buffer size info:" << std::endl;
  out << TAB "MinSize: " << info.BufferInfo.MinSize << std::endl;
  out << TAB "MaxSize: " << info.BufferInfo.MaxSize << std::endl;
  out << TAB "PrfSize: " << info.BufferInfo.PrefSize << std::endl;
  out << TAB "Granlty: " << info.BufferInfo.Granularity << std::endl;

  out << "SampleRate: " << info.SampleRate << std::endl;
}

const char* Helpers::ASIOErrorToStr(ASIOError error) {
#define CASEGEN(err) \
  case err:          \
    return #err;

  switch (error) {
    CASEGEN(ASE_OK);
    CASEGEN(ASE_SUCCESS);
    CASEGEN(ASE_NotPresent);
    CASEGEN(ASE_HWMalfunction);
    CASEGEN(ASE_InvalidParameter);
    CASEGEN(ASE_InvalidMode);
    CASEGEN(ASE_SPNotAdvancing);
    CASEGEN(ASE_NoClock);
    CASEGEN(ASE_NoMemory);

    default:
      return "Invalid ASIOError value";
  }

#undef CASEGEN
}

const char* Helpers::ASIOSampleTypeToStr(ASIOSampleType type) {
#define CASEGEN(type) \
  case type:          \
    return #type;

  switch (type) {
    CASEGEN(ASIOSTDSDInt8LSB1);
    CASEGEN(ASIOSTDSDInt8MSB1);
    CASEGEN(ASIOSTDSDInt8NER8);
    CASEGEN(ASIOSTFloat32LSB);
    CASEGEN(ASIOSTFloat32MSB);
    CASEGEN(ASIOSTFloat64LSB);
    CASEGEN(ASIOSTFloat64MSB);
    CASEGEN(ASIOSTInt16LSB);
    CASEGEN(ASIOSTInt16MSB);
    CASEGEN(ASIOSTInt24LSB);
    CASEGEN(ASIOSTInt24MSB);
    CASEGEN(ASIOSTInt32LSB16);
    CASEGEN(ASIOSTInt32LSB18);
    CASEGEN(ASIOSTInt32LSB20);
    CASEGEN(ASIOSTInt32LSB24);
    CASEGEN(ASIOSTInt32LSB);
    CASEGEN(ASIOSTInt32MSB16);
    CASEGEN(ASIOSTInt32MSB18);
    CASEGEN(ASIOSTInt32MSB20);
    CASEGEN(ASIOSTInt32MSB24);
    CASEGEN(ASIOSTInt32MSB);

    default:
      return "Invalid ASIOSampleType value";
  }

#undef CASEGEN
}

Helpers::AsioProcessorMock::AsioProcessorMock(decltype(ProcessInputFunc) pi,
                                              decltype(ProcessOutputFunc) po,
                                              decltype(ConfigureFunc) cf)
    : AsioContext::IProcessor{},
      ProcessInputFunc{pi},
      ProcessOutputFunc{po},
      ConfigureFunc{cf} {}

void Helpers::AsioProcessorMock::ProcessInput(long channel, void* buf,
                                              ASIOSampleType type) {
  ProcessInputFunc(channel, buf, type);
}

void Helpers::AsioProcessorMock::ProcessOutput(long channel, void* buf,
                                               ASIOSampleType type) {
  ProcessOutputFunc(channel, buf, type);
}

void Helpers::AsioProcessorMock::Configure(size_t bufSize, size_t nInputs,
                                           size_t nOutputs) {
  ConfigureFunc(bufSize, nInputs, nOutputs);
}

AsioContext::ProcessorT Helpers::AsioProcessorMock::Create(
    decltype(ProcessInputFunc) pi, decltype(ProcessOutputFunc) po,
    decltype(ConfigureFunc) cf) {
  return std::make_unique<AsioProcessorMock>(pi, po, cf);
};

Helpers::AsioHandlerMock::AsioHandlerMock(decltype(HandleFunc) handler)
    : AsioContext::IHandler{}, HandleFunc{handler} {}

void Helpers::AsioHandlerMock::HandleEvent(AsioContext::DriverEvent event) {
  HandleFunc(event);
}

AsioContext::HandlerT Helpers::AsioHandlerMock::Create(
    decltype(HandleFunc) handler) {
  return std::make_unique<AsioHandlerMock>(handler);
};

}  // namespace GigOn