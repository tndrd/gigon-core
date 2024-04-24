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

  // long as channel ID remains here to avoid narrowing conversions
  using ProcessCallback = std::function<void(
      long /* channel */, void* /* buffer */, ASIOSampleType /* type */)>;
  using DriverEventCallback = std::function<void(DriverEvent /* event */)>;

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

  void SetCallbacks(ProcessCallback inputCallback,
                    ProcessCallback outputCallback,
                    DriverEventCallback eventHandler);

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
void DumpAsioInfo(std::ostream& out, const ASIODriverInfo& info);
void DumpDeviceInfo(std::ostream& out,
                    const AsioContext::DeviceInformation& info);
const char* ASIOErrorToStr(ASIOError error);
const char* ASIOSampleTypeToStr(ASIOSampleType type);
}  // namespace Helpers

}  // namespace GigOn