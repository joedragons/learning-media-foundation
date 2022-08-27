#include "mf_scheduler.hpp"

#include <mfapi.h>
#include <mfidl.h> // for IMFRealTimeClientEx
#include <synchapi.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.system.h>

mf_scheduler_t::mf_scheduler_t() noexcept(false) {
    if (auto hr = MFAllocateWorkQueueEx(MF_STANDARD_WORKQUEUE, &queue); FAILED(hr))
        winrt::throw_hresult(hr);
}

mf_scheduler_t::~mf_scheduler_t() {
    MFUnlockWorkQueue(queue);
}

DWORD mf_scheduler_t::handle() const noexcept {
    return queue;
}

winrt::com_ptr<IMFAsyncResult> mf_scheduler_t::schedule(IMFAsyncCallback* callback, LONG priority) noexcept(false) {
    winrt::com_ptr<IMFAsyncResult> result{};
    if (auto hr = MFCreateAsyncResult(nullptr, callback, nullptr, result.put()); FAILED(hr))
        throw std::system_error{hr, std::system_category(), "MFCreateAsyncResult"};
    // MFPutWorkItemEx(queue, result.get())
    if (auto hr = MFPutWorkItemEx2(queue, priority, result.get()); FAILED(hr))
        throw std::system_error{hr, std::system_category(), "MFPutWorkItemEx2"};
    return result;
}

namespace std {

void lock_guard<mf_scheduler_t>::lock() noexcept(false) {
    winrt::check_hresult(MFLockWorkQueue(ref.handle()));
}

void lock_guard<mf_scheduler_t>::unlock() noexcept(false) {
    winrt::check_hresult(MFUnlockWorkQueue(ref.handle()));
}

} // namespace std