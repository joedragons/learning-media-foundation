#include <CppUnitTest.h>

#include <DispatcherQueue.h>
#include <VersionHelpers.h>
#include <experimental/coroutine>
#include <mfapi.h>
#include <pplawait.h>
#include <ppltasks.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <winrt/windows.foundation.h> // namespace winrt::Windows::Foundation
#include <winrt/windows.system.h>     // namespace winrt::Windows::System

// #include "winrt/WinRTComponent.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

bool has_env(const char* key) noexcept {
    size_t len = 0;
    char buf[40]{};
    if (auto ec = getenv_s(&len, buf, key); ec != 0)
        return false;
    std::string_view value{buf, len};
    return value.empty() == false;
}

/**
 * @brief Redirect spdlog messages to `Logger::WriteMessage`
 */
class vstest_sink final : public spdlog::sinks::sink {
    std::unique_ptr<spdlog::formatter> formatter;

  public:
    void log(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t buf{};
        formatter->format(msg, buf);
        std::string txt = fmt::to_string(buf);
        Logger::WriteMessage(txt.c_str());
    }
    void flush() override {
        Logger::WriteMessage(L"\n");
    }
    void set_pattern(const std::string& p) override {
        formatter = std::make_unique<spdlog::pattern_formatter>(p);
    }
    void set_formatter(std::unique_ptr<spdlog::formatter> f) override {
        formatter = std::move(f);
    }
};

auto make_logger(const char* name, FILE* fout) noexcept(false) {
    spdlog::sink_ptr sink0 = [fout]() -> spdlog::sink_ptr {
        using mutex_t = spdlog::details::console_nullmutex;
        if (fout == stdout || fout == stderr)
            return std::make_shared<spdlog::sinks::stdout_color_sink_st>();
        using sink_t = spdlog::sinks::stdout_sink_base<mutex_t>;
        return std::make_shared<sink_t>(fout);
    }();
    spdlog::sink_ptr sink1 = std::make_shared<vstest_sink>();
    return std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{sink0, sink1});
}

/**
 * @see https://docs.microsoft.com/en-us/windows/win32/sysinfo/getting-the-system-version
 */
TEST_MODULE_INITIALIZE(Initialize) {
    // change default logger to use vstest_sink
    auto logger = make_logger("test", stdout);
    logger->set_pattern("%T.%e [%L] %8t %v");
    logger->set_level(spdlog::level::level_enum::debug);
    spdlog::set_default_logger(logger);
    // Default is `multi_threaded`...
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::check_hresult(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    spdlog::info("C++/WinRT:");
    spdlog::info("  version: {:s}", CPPWINRT_VERSION); // WINRT_version
    // spdlog::info("Windows Media Foundation:");
    // spdlog::info("  SDK: {:X}", MF_SDK_VERSION);
    // spdlog::info("  API: {:X}", MF_API_VERSION);
}

TEST_MODULE_CLEANUP(Cleanup) {
    MFShutdown();
    winrt::uninit_apartment();
}

using std::experimental::coroutine_handle;
using winrt::Windows::Foundation::IAsyncAction;
using winrt::Windows::Foundation::IAsyncOperation;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

class winrt_test_case : public TestClass<winrt_test_case> {
    winrt::Windows::System::DispatcherQueueController controller = nullptr;
    winrt::Windows::System::DispatcherQueue queue = nullptr;

    /// @see https://devblogs.microsoft.com/oldnewthing/20191223-00/?p=103255
    [[nodiscard]] static auto resume_on_queue(winrt::Windows::System::DispatcherQueue queue) {
        struct awaitable_t final {
            winrt::Windows::System::DispatcherQueue worker;

            bool await_ready() const noexcept {
                return worker == nullptr;
            }
            bool await_suspend(coroutine_handle<void> task) noexcept {
                return worker.TryEnqueue(task);
            }
            constexpr void await_resume() const noexcept {
            }
        };
        return awaitable_t{queue};
    }

    /// @throws winrt::hresult_error
    [[nodiscard]] static auto query_thread_id(winrt::Windows::System::DispatcherQueue queue) noexcept(false)
        -> IAsyncOperation<uint32_t> {
        //co_await winrt::resume_foreground(queue);
        co_await resume_on_queue(queue);
        co_return GetCurrentThreadId();
    }

  public:
    ~winrt_test_case() noexcept = default;

    TEST_METHOD_INITIALIZE(setup) {
        controller = DispatcherQueueController::CreateOnDedicatedThread();
        queue = controller.DispatcherQueue();
    }
    TEST_METHOD_CLEANUP(teardown) {
        IAsyncAction operation = controller.ShutdownQueueAsync();
        operation.get();
    }

    TEST_METHOD(test_query_thread_id) {
        DWORD current = GetCurrentThreadId();
        DWORD dedicated = query_thread_id(queue).get();
        Assert::AreNotEqual<DWORD>(current, dedicated);
    }
};
