// asiosys needs to be included first
// clang-format off
#include "asiosdk/common/asiosys.h"
#include "asiosdk/common/asio.h"
#include "asiosdk/host/asiodrivers.h"
// clang-format on

#include "conio.h"

// #include "portable_endian.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#undef min
#undef max

#define TAB "   "
#define LINE "--------------------------------------"

const size_t ASIO_DRIVER_NAME_LEN = 32;
const size_t NAMES_TO_REQUEST = 10;
const auto DRIVER_NAME = "Focusrite USB ASIO";

// Struct containing static driver data
struct DeviceInformation {
  struct {
    long Input = 0;
    long Output = 0;
  } NumChannels;

  std::vector<ASIOChannelInfo> Inputs;
  std::vector<ASIOChannelInfo> Outputs;

  struct {
    long MinSize = 0;
    long MaxSize = 0;
    long PrefSize = 0;
    long Granularity = 0;
  } BufferInfo;

  ASIOSampleRate SampleRate = 0;
};

const char* ASIOErrorToStr(ASIOError error) {
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

const char* ASIOSampleTypeToStr(ASIOSampleType type) {
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

struct AsioException : public std::runtime_error {
  AsioException(const std::string& msg, ASIOError errorCode)
      : std::runtime_error{msg + ": " + ASIOErrorToStr(errorCode)} {}
  virtual ~AsioException() {}
};

AsioDrivers& GetAsioDrivers() {
  static AsioDrivers drivers{};
  return drivers;
}

class AsioContext final {
 public:
  enum class DriverEvent { Overload };

  using ProcessCallback = std::function<void(
      long /* channel */, void* /* buffer */, ASIOSampleType /* type */)>;
  using DriverEventCallback = std::function<void(DriverEvent)>;

 private:
  struct Msg {
    static constexpr auto AlreadyLoaded = "Driver already loaded";
    static constexpr auto NotLoaded = "Driver not loaded";
    static constexpr auto AlreadyInit = "Driver already initialized";
    static constexpr auto NotInit = "Driver not initialized";
    static constexpr auto BuffersPresent = "Buffers already created";
    static constexpr auto BuffersAbsent = "Buffers are not created";
    static constexpr auto AlreadyRunning = "Driver is already running";
    static constexpr auto NotRunning = "Driver is not running";
    static constexpr auto NoCallbacksSet = "Callbacks are not set";
  };

  ASIODriverInfo AsioInfo{};
  std::vector<ASIOBufferInfo> AsioBufferInfos;

  DeviceInformation DeviceInfo;
  bool PostOutput = false;

  struct BuffersInfo {
    long NumInput = 0;
    long NumOutput = 0;
    long BufferSize = 0;
  } ActiveBuffersInfo;

  ASIOCallbacks AsioCallbacks;

  struct {
    ProcessCallback ProcessInput;
    ProcessCallback ProcessOutput;
    DriverEventCallback HandleEvent;
  } UserCallbacks;

  bool Loaded = false;
  bool Initialized = false;
  bool CallbacksSet = false;
  bool BuffersCreated = false;
  bool Started = false;

  // Private ctor
  AsioContext() {
    AsioCallbacks.asioMessage = AsioMessageCallback;
    AsioCallbacks.bufferSwitch = AsioBufferSwitchCallback;
    AsioCallbacks.bufferSwitchTimeInfo = AsioBufferSwitchTimeInfoCallback;
    AsioCallbacks.sampleRateDidChange = AsioSampleRateChangedCallback;
  }

  // Because it's a singleton, we do not need to move it
  // or copy, therefore these four should be deleted
  AsioContext(const AsioContext&) = delete;
  AsioContext& operator=(const AsioContext&) = delete;

  AsioContext(AsioContext&&) = delete;
  AsioContext& operator=(AsioContext&&) = delete;

 public:
  static AsioContext& Get() {
    static AsioContext ctx;
    return ctx;
  }

  void LoadDriver(const std::string& driverName) {
    Expect(!Loaded, Msg::AlreadyLoaded);

    std::string tmp = driverName;

    if (!GetAsioDrivers().loadDriver(&tmp[0]))
      throw std::runtime_error("Failed to load driver \"" + driverName + "\"");

    Loaded = true;
  }

  void InitDriver() {
    Expect(Loaded, Msg::NotLoaded);
    Expect(!Initialized, Msg::AlreadyLoaded);

    ASIOError status = ASIOInit(&AsioInfo);
    if (status != ASE_OK) throw AsioException("Failed to init driver", status);

    DeviceInfo = GetDeviceInfoInternal();
    PostOutput = ASIOOutputReady() == ASE_OK;

    Initialized = true;
  }

  void SetCallbacks(ProcessCallback inputCallback,
                    ProcessCallback outputCallback,
                    DriverEventCallback eventHandler) {
    Expect(Initialized, Msg::NotInit);
    Expect(!BuffersCreated, Msg::BuffersPresent);

    UserCallbacks.ProcessInput = inputCallback;
    UserCallbacks.ProcessOutput = outputCallback;
    UserCallbacks.HandleEvent = eventHandler;

    CallbacksSet = true;
  }

  void CreateBuffers(const std::vector<long>& inputs,
                     const std::vector<long>& outputs, long bufferSize) {
    Expect(CallbacksSet, Msg::NoCallbacksSet);
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

    ASIOError status = ASIOCreateBuffers(binfos.data(), binfos.size(),
                                         bufferSize, &AsioCallbacks);
    if (status != ASE_OK) throw AsioException("ASIOCreateBuffers()", status);

    // Kalb line here

    ActiveBuffersInfo.NumInput = inputs.size();
    ActiveBuffersInfo.NumOutput = outputs.size();
    ActiveBuffersInfo.BufferSize = bufferSize;

    AsioBufferInfos = std::move(binfos);

    BuffersCreated = true;
  }

  void Start() {
    Expect(BuffersCreated, Msg::BuffersAbsent);
    Expect(!Started, Msg::AlreadyRunning);

    ASIOError status = ASIOStart();
    if (status != ASE_OK) throw AsioException("ASIOStart()", status);

    Started = true;
  }

  void Stop() {
    Expect(Started, Msg::NotRunning);

    ASIOError status = ASIOStop();
    if (status != ASE_OK) throw AsioException("ASIOStop()", status);

    Started = false;
  }

  void DisposeBuffers() {
    Expect(BuffersCreated, Msg::BuffersAbsent);
    Expect(!Started, Msg::AlreadyRunning);

    ASIOError status = ASIODisposeBuffers();
    if (status != ASE_OK) throw AsioException("ASIODisposeBuffers()", status);

    BuffersCreated = false;
  }

  void DeInitDriver() {
    Expect(Initialized, Msg::NotInit);
    Expect(!BuffersCreated, Msg::BuffersPresent);

    ASIOError status = ASIOExit();
    if (status != ASE_OK) throw AsioException("ASIOExit()", status);

    Initialized = false;
  }

  void UnloadDriver() {
    Expect(Loaded, Msg::NotLoaded);
    Expect(!Initialized, Msg::AlreadyInit);

    GetAsioDrivers().removeCurrentDriver();
    Loaded = false;
  }

  DeviceInformation GetDeviceInfo() const {
    Expect(Initialized, Msg::NotInit);

    return DeviceInfo;
  }

  ASIODriverInfo GetAsioInfo() const {
    Expect(Initialized, Msg::NotInit);

    return AsioInfo;
  }

  bool DriverControlPanel() {
    Expect(Initialized, Msg::NotInit);

    ASIOError status = ASIOControlPanel();
    if (status == ASE_OK) return true;

    if (status == ASE_NotPresent) return false;

    throw AsioException("ASIOControlPanel()", status);
  }

  BuffersInfo GetBuffersInfo() const {
    Expect(BuffersCreated, Msg::BuffersAbsent);

    return ActiveBuffersInfo;
  }

 private:
  void Expect(bool var, const char* msg) const {
    if (!var) throw std::runtime_error(msg);
  }

  DeviceInformation GetDeviceInfoInternal() const {
    assert(Loaded);

    ASIOError status;
    DeviceInformation info;

    auto& numInputs = info.NumChannels.Input;
    auto& numOutputs = info.NumChannels.Output;
    auto& minSize = info.BufferInfo.MinSize;
    auto& maxSize = info.BufferInfo.MaxSize;
    auto& prefSize = info.BufferInfo.PrefSize;
    auto& granularity = info.BufferInfo.Granularity;

    if ((status = ASIOGetChannels(&numInputs, &numOutputs)) != ASE_OK)
      throw AsioException("ASIOGetChannels", status);

    for (size_t i = 0; i < numInputs + numOutputs; ++i) {
      ASIOChannelInfo chinfo;
      chinfo.isInput = i < numInputs;
      chinfo.channel = chinfo.isInput ? i : i - numInputs;

      if ((status = ASIOGetChannelInfo(&chinfo)) != ASE_OK)
        throw AsioException("ASIOGetChannelInfo()", status);

      if (chinfo.isInput) {
        info.Inputs.push_back(chinfo);
      } else {
        info.Outputs.push_back(chinfo);
      }
    }

    if ((status = ASIOGetBufferSize(&minSize, &maxSize, &prefSize,
                                    &granularity)) != ASE_OK)
      throw AsioException("AsioGetBufferSize()", status);

    if ((status = ASIOGetSampleRate(&info.SampleRate)) != ASE_OK)
      throw AsioException("ASIOGetSampleRate()", status);

    return info;
  }

  void CheckBufferSize(long bufSize) {
    assert(Initialized);

    bool cond1 = bufSize < DeviceInfo.BufferInfo.MinSize;
    bool cond2 = bufSize > DeviceInfo.BufferInfo.MaxSize;
    bool cond3 = !!(bufSize % DeviceInfo.BufferInfo.Granularity);

    if (cond1 || cond2 || cond3)
      throw std::runtime_error("Incorrect buffer size");
  }

  // Actual processing callback. Is called when all the buffers are about
  // to be switched, so we need to take the data from inputs and put it
  // to outputs.
  // For now this callback delegates everything to user callbacks via
  // std::function. It could be changed to template-based strategy
  // if profiling reveals such neccessity
  static void AsioBufferSwitchCallback(long index, ASIOBool processNow) {
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
        asio.UserCallbacks.ProcessInput(channel, bufPtr, channelInfo.type);
      else
        asio.UserCallbacks.ProcessOutput(channel, bufPtr, channelInfo.type);
    }

    if (asio.PostOutput) ASIOOutputReady();
  }

  // In fact, we do not need to process time information for now, so we simply
  // redirect to simplier handler
  static ASIOTime* AsioBufferSwitchTimeInfoCallback(ASIOTime* timeInfo,
                                                    long index,
                                                    ASIOBool processNow) {
    AsioBufferSwitchCallback(index, processNow);
    return nullptr;
  }

  static void AsioSampleRateChangedCallback(ASIOSampleRate sRate) {
    assert(0 && "Not yet implemented");
  }

  static long AsioMessageCallback(long selector, long value, void* message,
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
        AsioContext::Get().UserCallbacks.HandleEvent(
            AsioContext::DriverEvent::Overload);
        break;
    }

    return ret;
  }

  ASIOError DtorStopDriver() {
    if (!Started) return ASE_OK;
    return ASIOStop();
  }

  ASIOError DtorDisposeBuffers() {
    if (!BuffersCreated) return ASE_OK;
    return ASIODisposeBuffers();
  }

  ASIOError DtorExitDriver() {
    if (!Initialized) return ASE_OK;
    return ASIOExit();
  }

  ~AsioContext() {
    ASIOError status = ASE_OK;

    if ((status = DtorStopDriver()) != ASE_OK)
      std::cout << "ASIOStop: " << ASIOErrorToStr(status)
                << std::endl;  // TODO LOGGER

    if ((status = DtorDisposeBuffers()) != ASE_OK)
      std::cout << "ASIODisposeBuffers: " << ASIOErrorToStr(status)
                << std::endl;  // TODO LOGGER

    if ((status = DtorExitDriver()) != ASE_OK)
      std::cout << "ASIOExit: " << ASIOErrorToStr(status)
                << std::endl;  // TODO LOGGER

    GetAsioDrivers().removeCurrentDriver();
  }
};

std::vector<std::string> GetDriverNames(size_t maxNames) {
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

void DumpAsioInfo(std::ostream& out, const ASIODriverInfo& info) {
  out << "ASIO Driver info dump: " << std::endl;
  out << TAB "Driver name:   " << info.name << std::endl;
  out << TAB "Error message: " << info.errorMessage << std::endl;
}

void DumpDeviceInfo(std::ostream& out, const DeviceInformation& info) {
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

void DisplayValue(float value) {
  float logged = std::log1pf(value * exp(1));

  int norm = logged * 50;

  int i = 0;
  std::cout << "\r|";
  for (; i < norm; ++i) std::cout << "#";
  for (; i < 50; ++i) std::cout << " ";
  std::cout << "|";
}

int main() try {
  // Get driver names
  auto drivers = GetDriverNames(NAMES_TO_REQUEST);

  if (drivers.size() == 0) {
    std::cout << "Failed to load drivers" << std::endl;
    return 1;
  }

  long bufSize = 64;
  float average;

  auto inputCallback = [bufSize, &average](long channel, void* buffer,
                                           ASIOSampleType stype) -> void {
    assert(stype == ASIOSTInt32LSB);
    assert(buffer);

    auto samplePtr = reinterpret_cast<int32_t*>(buffer);

    average = 0;

    for (int i = 0; i < bufSize; ++i, ++samplePtr) {
      int32_t max = std::numeric_limits<int32_t>::max();
      int32_t sample = *samplePtr;  // le32toh(*samplePtr);

      average += abs(float(sample) / max);
    }

    average /= bufSize;
  };

  auto outputCallback = [bufSize](long channel, void* buffer,
                                  ASIOSampleType stype) -> void {};

  auto eventCallback = [](AsioContext::DriverEvent event) -> void {
    std::cout << "Overload happened!" << std::endl;
    assert(0);
  };

  std::string driverName = DRIVER_NAME;

  auto& asio = AsioContext::Get();

  asio.LoadDriver(driverName);
  asio.InitDriver();

  asio.SetCallbacks(inputCallback, outputCallback, eventCallback);
  asio.CreateBuffers({1}, {1}, bufSize);

  DumpAsioInfo(std::cout, asio.GetAsioInfo());
  DumpDeviceInfo(std::cout, asio.GetDeviceInfo());

  std::cout << "Press [Enter] to start..." << std::endl;
  getchar();
  asio.Start();

  while (true) {
    DisplayValue(average);
    Sleep(30);
  }

  asio.Stop();
  asio.DisposeBuffers();
  asio.DeInitDriver();
  asio.UnloadDriver();

} catch (std::exception& e) {
  std::cout << e.what() << std::endl;
}