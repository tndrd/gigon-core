#pragma once

// asiosys needs to be included first
// clang-format off
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
// clang-format on

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace GigOn {

class AsioContext final {
 public:
  enum class DriverEvent { Overload };

  struct Exception : public std::runtime_error {
    Exception(const std::string& msg, ASIOError errorCode);
    virtual ~Exception();
  };

  struct DeviceInformation {
    struct {
      size_t Input = 0;
      size_t Output = 0;
    } NumChannels;

    std::vector<ASIOChannelInfo> Inputs;
    std::vector<ASIOChannelInfo> Outputs;

    struct {
      size_t MinSize = 0;
      size_t MaxSize = 0;
      size_t PrefSize = 0;
      size_t Granularity = 0;
    } BufferInfo;

    ASIOSampleRate SampleRate = 0;
  };

  using ChannelId = size_t;

  struct IProcessor {
    virtual void Configure(size_t bufSize, size_t nInputs, size_t nOutputs) = 0;
    virtual void ProcessInput(long channel, void* buffer,
                              ASIOSampleType type) = 0;
    virtual void ProcessOutput(long channel, void* buffer,
                               ASIOSampleType type) = 0;
    virtual ~IProcessor() = default;
  };

  struct IHandler {
    virtual void HandleEvent(DriverEvent event) = 0;
    virtual ~IHandler() = default;
  };

  using ProcessorT = std::unique_ptr<IProcessor>;
  using HandlerT = std::unique_ptr<IHandler>;

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
    static constexpr auto NoHandlersSet = "Handlers are not set";
  };

 private:
  ASIODriverInfo AsioInfo{};
  std::vector<ASIOBufferInfo> AsioBufferInfos;

  DeviceInformation DeviceInfo;
  bool PostOutput = false;

  struct BuffersInformation {
    size_t NumInput = 0;
    size_t NumOutput = 0;
    size_t BufferSize = 0;
  } ActiveBuffersInfo;

  ASIOCallbacks AsioCallbacks;

  ProcessorT Processor;
  HandlerT Handler;

  bool Loaded = false;
  bool Initialized = false;
  bool HandlersSet = false;
  bool BuffersCreated = false;
  bool Started = false;

 private:
  // Private ctor
  AsioContext();

  // Private dtor
  ~AsioContext();

  // Because it's a singleton, we do not need to move it
  // or copy, therefore these four should be deleted
  AsioContext(const AsioContext&) = delete;
  AsioContext& operator=(const AsioContext&) = delete;

  AsioContext(AsioContext&&) = delete;
  AsioContext& operator=(AsioContext&&) = delete;

 public:
  static AsioContext& Get();

  void LoadDriver(const std::string& driverName);
  void UnloadDriver();

  void InitDriver();
  void DeInitDriver();

  void SetHandlers(ProcessorT&& processor, HandlerT&& handler);

  void CreateBuffers(const std::vector<ChannelId>& inputs,
                     const std::vector<ChannelId>& outputs, size_t bufferSize);
  void DisposeBuffers();

  void Start();
  void Stop();

  DeviceInformation GetDeviceInfo() const;
  ASIODriverInfo GetAsioInfo() const;
  BuffersInformation GetBuffersInfo() const;

  static std::vector<std::string> GetDriverNames(size_t maxNames);

 private:
  static AsioDrivers& GetAsioDrivers();
  static void Expect(bool var, const char* msg);

  DeviceInformation GetDeviceInfoInternal() const;
  void CheckBufferSize(long bufSize) const;

  // Actual processing callback. Is called when all the buffers are about
  // to be switched, so we need to take the data from inputs and put it
  // to outputs.
  // For now this callback delegates everything to user callbacks via
  // std::function. It could be changed to template-based strategy
  // if profiling reveals such neccessity
  static void AsioBufferSwitchCallback(long index, ASIOBool processNow);

  // In fact, we do not need to process time information for now, so we simply
  // redirect to simplier handler
  static ASIOTime* AsioBufferSwitchTimeInfoCallback(ASIOTime* timeInfo,
                                                    long index,
                                                    ASIOBool processNow);

  static void AsioSampleRateChangedCallback(ASIOSampleRate sRate);

  static long AsioMessageCallback(long selector, long value, void* message,
                                  double* opt);

  ASIOError DtorStopDriver();
  ASIOError DtorDisposeBuffers();
  ASIOError DtorExitDriver();
};

namespace Helpers {
struct AsioProcessorMock final : public AsioContext::IProcessor {
 private:
  std::function<void(long, void*, ASIOSampleType)> ProcessInputFunc;
  std::function<void(long, void*, ASIOSampleType)> ProcessOutputFunc;
  std::function<void(size_t, size_t, size_t)> ConfigureFunc;

 public:
  AsioProcessorMock(decltype(ProcessInputFunc), decltype(ProcessOutputFunc),
                  decltype(ConfigureFunc));

  void ProcessInput(long channel, void* buf, ASIOSampleType type) override;
  void ProcessOutput(long channel, void* buf, ASIOSampleType type) override;
  void Configure(size_t bufSize, size_t nInputs, size_t nOutputs) override;

  static AsioContext::ProcessorT Create(decltype(ProcessInputFunc),
                                      decltype(ProcessOutputFunc),
                                      decltype(ConfigureFunc));
};

struct AsioHandlerMock final : public AsioContext::IHandler {
 private:
  std::function<void(AsioContext::DriverEvent)> HandleFunc;

 public:
  AsioHandlerMock(decltype(HandleFunc));
  void HandleEvent(AsioContext::DriverEvent) override;

  static AsioContext::HandlerT Create(decltype(HandleFunc));
};

void DumpAsioInfo(std::ostream& out, const ASIODriverInfo& info);
void DumpDeviceInfo(std::ostream& out,
                    const AsioContext::DeviceInformation& info);
const char* ASIOErrorToStr(ASIOError error);
const char* ASIOSampleTypeToStr(ASIOSampleType type);
}  // namespace Helpers

}  // namespace GigOn