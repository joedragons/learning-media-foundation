/**
 * @see https://docs.microsoft.com/en-us/visualstudio/test/microsoft-visualstudio-testtools-cppunittestframework-api-reference
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-headers-and-libraries
 */
#include <CppUnitTest.h>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <winrt/Windows.System.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/**
 * @brief Redirect spdlog messages to `Logger::WriteMessage`
 */
class vstest_sink : public spdlog::sinks::sink {
    std::unique_ptr<spdlog::formatter> formatter;

  public:
    void log(const spdlog::details::log_msg& msg) final {
        spdlog::memory_buf_t buf{};
        formatter->format(msg, buf);
        std::string txt = fmt::to_string(buf);
        Logger::WriteMessage(txt.c_str());
    }
    void flush() final {
        Logger::WriteMessage(L"\n");
    }
    void set_pattern(const std::string& p) final {
        formatter = std::make_unique<spdlog::pattern_formatter>(p);
    }
    void set_formatter(std::unique_ptr<spdlog::formatter> f) final {
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

TEST_MODULE_INITIALIZE(Initialize) {
    try {
        // Default is `multi_threaded`...
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::check_hresult(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    } catch (const winrt::hresult_error& ex) {
        Assert::Fail(ex.message().c_str());
        return;
    }
    auto logger = make_logger("test", stdout);
    logger->set_pattern("%T.%e [%L] %8t %v");
    logger->set_level(spdlog::level::level_enum::debug);
    spdlog::set_default_logger(logger);
}

TEST_MODULE_CLEANUP(Cleanup) {
    MFShutdown();
    winrt::uninit_apartment();
}

class test_case : public ::Microsoft::VisualStudio::CppUnitTestFramework::TestClass<test_case> {
  public:
    TEST_CLASS_INITIALIZE(Initialize) {
        spdlog::info("{}: {}", "test_case", "Initialize");
    }
    TEST_CLASS_CLEANUP(Cleanup) {
        spdlog::info("{}: {}", "test_case", "Cleanup");
    }

  public:
    TEST_METHOD_INITIALIZE(setup) {
        spdlog::info("{}: {}", "test_case", "setup");
    }
    TEST_METHOD_CLEANUP(teardown) {
        spdlog::info("{}: {}", "test_case", "teardown");
    }

    TEST_METHOD(method0) {
        Assert::AreEqual(0, 0);
    }

    TEST_METHOD(method1) {
        Assert::AreEqual(10, 10);
    }
};
