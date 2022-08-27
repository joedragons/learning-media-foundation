/**
 * @see https://docs.microsoft.com/en-us/visualstudio/test/microsoft-visualstudio-testtools-cppunittestframework-api-reference
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-headers-and-libraries
 */
#include <CppUnitTest.h>

#include <Mferror.h>
#include <mfapi.h>
#include <mfidl.h>
#include <spdlog/spdlog.h>
#include <winrt/windows.system.h>

#include "mf_scheduler.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

class mf_scheduler_test_case : public TestClass<mf_scheduler_test_case> {
  private:
    std::unique_ptr<mf_scheduler_t> scheduler = nullptr;

  public:
    ~mf_scheduler_test_case() noexcept = default;

    // TEST_CLASS_INITIALIZE(Initialize) {
    //     spdlog::info("{}: {}", "test_case", "Initialize");
    // }
    // TEST_CLASS_CLEANUP(Cleanup) {
    //     spdlog::info("{}: {}", "test_case", "Cleanup");
    // }

    TEST_METHOD_INITIALIZE(setup) {
        scheduler = std::make_unique<mf_scheduler_t>();
    }
    TEST_METHOD_CLEANUP(teardown) {
        scheduler = nullptr;
    }

    TEST_METHOD(test_scheduler_handle) {
        std::lock_guard lck{*scheduler};
        Assert::AreNotEqual<DWORD>(0, scheduler->handle());
    }
};
