#pragma once
#include <mutex>
#include <winrt/base.h>

#include <mfobjects.h> // um/mfobjects.h

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/work-queues
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/using-work-queues
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-work-queue-and-threading-improvements
class mf_scheduler_t final {
    DWORD queue = 0;

  public:
    mf_scheduler_t() noexcept(false);
    mf_scheduler_t(const mf_scheduler_t&) noexcept(false);
    mf_scheduler_t(mf_scheduler_t&&) = delete;
    mf_scheduler_t& operator=(const mf_scheduler_t&) = delete;
    mf_scheduler_t& operator=(mf_scheduler_t&&) = delete;
    ~mf_scheduler_t() noexcept;

    DWORD handle() const noexcept;

    winrt::com_ptr<IMFAsyncResult> schedule(IMFAsyncCallback* callback, LONG priority) noexcept(false);
};

namespace std {
template <>
struct lock_guard<mf_scheduler_t> {
    mf_scheduler_t& ref;

  public:
    void lock() noexcept(false);
    void unlock() noexcept(false);
};
} // namespace std
