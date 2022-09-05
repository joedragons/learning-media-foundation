#include <CppUnitTest.h>

#include <d3d11_4.h>
#include <d3d9.h>
#include <evr.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windowsx.h>
#include <winrt/Windows.Foundation.h>

#include <experimental/generator>
#include <filesystem>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include "mf_transform.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

void print(IMFMediaType* media_type) noexcept;
std::string to_mf_string(const GUID& guid) noexcept;

void report_error(HRESULT hr, const char* fname, const spdlog::source_loc& loc) noexcept {
    winrt::hresult_error ex{hr};
    spdlog::log(loc, spdlog::level::err, //
                "{}: {:#08x} {}", fname, static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
}

HRESULT make_texture(ID3D11Device* device, ID3D11Texture2D** texture) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 256;
    desc.Height = 256;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;                     // todo: use D3D11_CPU_ACCESS_WRITE?
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // todo: use D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX?
    return device->CreateTexture2D(&desc, nullptr, texture);
}

HRESULT resolve(const std::wstring& fpath, IMFMediaSourceEx** source, MF_OBJECT_TYPE& media_object_type) noexcept {
    winrt::com_ptr<IMFSourceResolver> resolver{};
    if (auto hr = MFCreateSourceResolver(resolver.put()); FAILED(hr))
        return hr;
    winrt::com_ptr<IUnknown> unknown{};
    if (auto hr = resolver->CreateObjectFromURL(fpath.c_str(), MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_READ, nullptr,
                                                &media_object_type, unknown.put());
        FAILED(hr))
        return hr;
    return unknown->QueryInterface(source);
}

HRESULT consume(winrt::com_ptr<IMFSourceReaderEx> source_reader, DWORD istream, size_t& count) {
    bool input_available = true;
    while (input_available) {
        DWORD stream_index{};
        DWORD sample_flags{};
        LONGLONG sample_timestamp = 0; // unit 100-nanosecond
        winrt::com_ptr<IMFSample> input_sample{};
        if (auto hr = source_reader->ReadSample(istream, 0, &stream_index, &sample_flags, &sample_timestamp,
                                                input_sample.put());
            FAILED(hr)) {
            // CAPTURE(sample_flags);
            return hr;
        }
        if (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            input_available = false;
            continue;
        }
        // probably MF_SOURCE_READERF_STREAMTICK
        if (input_sample == nullptr)
            continue;
        input_sample->SetSampleTime(sample_timestamp);
        ++count;
    }
    return S_OK;
}

auto read_samples(winrt::com_ptr<IMFSourceReaderEx> reader, DWORD stream_index)
    -> std::experimental::generator<winrt::com_ptr<IMFSample>> {
    while (true) {
        DWORD actual_index{};
        DWORD flags{};
        LONGLONG timestamp = 0; // unit 100-nanosecond
        winrt::com_ptr<IMFSample> sample{};
        if (auto hr = reader->ReadSample(stream_index, 0, &actual_index, &flags, &timestamp, sample.put());
            FAILED(hr)) {
            spdlog::error("{}: {:#08x}", "ReadSample", hr);
            co_return;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            co_return;
        // probably MF_SOURCE_READERF_STREAMTICK
        if (sample == nullptr)
            continue;
        sample->SetSampleTime(timestamp);
        co_yield sample;
    }
}

winrt::com_ptr<IMFMediaType> clone(IMFMediaType* input) noexcept(false) {
    winrt::com_ptr<IMFMediaType> output{};
    if (auto hr = MFCreateMediaType(output.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = input->CopyAllItems(output.get()); FAILED(hr))
        winrt::throw_hresult(hr);
    return output;
}

winrt::com_ptr<IMFMediaType> make_video_type(IMFMediaType* input, const GUID& subtype) noexcept {
    winrt::com_ptr<IMFMediaType> output = clone(input);
    if (auto hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = output->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
        winrt::throw_hresult(hr);
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(input, MF_MT_FRAME_SIZE, &w, &h);
    if (auto hr = MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, w, h); FAILED(hr))
        winrt::throw_hresult(hr);
    UINT32 num = 0, denom = 1;
    MFGetAttributeRatio(input, MF_MT_FRAME_RATE, &num, &denom);
    if (auto hr = MFSetAttributeSize(output.get(), MF_MT_FRAME_RATE, num, denom); FAILED(hr))
        winrt::throw_hresult(hr);
    return output;
}

/// @see https://docs.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imfmediabuffer
HRESULT create_single_buffer_sample(IMFSample** output, DWORD bufsz) {
    if (auto hr = MFCreateSample(output); FAILED(hr))
        return hr;
    winrt::com_ptr<IMFMediaBuffer> buffer{};
    if (auto hr = MFCreateMemoryBuffer(bufsz, buffer.put()); FAILED(hr))
        return hr;
    // GetMaxLength will be length of the available memory location
    // GetCurrentLength will be 0
    IMFSample* sample = *output;
    return sample->AddBuffer(buffer.get());
}

/// @see https://docs.microsoft.com/en-us/windows/win32/api/evr/nc-evr-mfcreatevideosamplefromsurface
HRESULT make_texture_surface(ID3D11Texture2D* tex2d, IMFSample** ptr) {
    // Create a DXGI media buffer by calling the MFCreateDXGISurfaceBuffer function.
    // Pass in the ID3D11Texture2D pointer and the offset for each element in the texture array.
    // The function returns an IMFMediaBuffer pointer.
    winrt::com_ptr<IMFMediaBuffer> buffer{};
    if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d, 0, false, buffer.put()); FAILED(hr))
        return hr;

    // Create an empty media sample by calling the MFCreateVideoSampleFromSurface function.
    // Set the pUnkSurface parameter equal to NULL. The function returns an IMFSample pointer.
    winrt::com_ptr<IMFSample> sample{};
    if (auto hr = MFCreateVideoSampleFromSurface(nullptr, sample.put()); FAILED(hr))
        return hr;

    // Call IMFSample::AddBuffer to add the media buffer to the sample.
    auto hr = sample->AddBuffer(buffer.get());
    if (SUCCEEDED(hr)) {
        *ptr = sample.get();
        sample->AddRef();
    }
    return hr;
}

class video_buffer_test_case : public TestClass<video_buffer_test_case> {
    winrt::com_ptr<ID3D11Device> device{};
    D3D_FEATURE_LEVEL device_feature_level{};
    winrt::com_ptr<ID3D11DeviceContext> device_context{};

  public:
    ~video_buffer_test_case() noexcept = default;
    TEST_METHOD_INITIALIZE(setup) {
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                                            D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                        levels, 2, D3D11_SDK_VERSION, device.put(), &device_feature_level,
                                        device_context.put());
            FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        winrt::com_ptr<ID3D10Multithread> threading{};
        if (auto hr = device->QueryInterface(threading.put()); SUCCEEDED(hr))
            threading->SetMultithreadProtected(true);
    }

  public:
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/uncompressed-video-buffers
    TEST_METHOD(test_uncompressed_video_buffer_0) {
        winrt::com_ptr<ID3D11Texture2D> tex2d{};
        if (auto hr = make_texture(device.get(), tex2d.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        winrt::com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d.get(), 0, false, buffer.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());

        winrt::com_ptr<IMFDXGIBuffer> dxgi{};
        Assert::AreEqual(buffer->QueryInterface(dxgi.put()), S_OK);
        // should be same with `tex2d` above
        winrt::com_ptr<ID3D11Texture2D> texture{};
        Assert::AreEqual(dxgi->GetResource(IID_PPV_ARGS(texture.put())), S_OK);

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        Assert::AreEqual<uint32_t>(desc.Format, DXGI_FORMAT_B8G8R8A8_UNORM);
    }

    TEST_METHOD(test_uncompressed_video_buffer_1) {
        winrt::com_ptr<ID3D11Texture2D> tex2d{};
        if (auto hr = make_texture(device.get(), tex2d.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        winrt::com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d.get(), 0, false, buffer.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());

        winrt::com_ptr<IMF2DBuffer2> buf2d{};
        Assert::AreEqual(buffer->QueryInterface(buf2d.put()), S_OK);
    }

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/directx-surface-buffer
    //TEST_METHOD(test_uncompressed_video_buffer_2) {
    //    winrt::com_ptr<ID3D11Texture2D> tex2d{};
    //    if (auto hr = make_texture(device.get(), tex2d.put()); FAILED(hr))
    //        Assert::Fail(winrt::hresult_error{hr}.message().c_str());
    //    winrt::com_ptr<IMFMediaBuffer> buffer{};
    //    if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d.get(), 0, false, buffer.put()); FAILED(hr))
    //        Assert::Fail(winrt::hresult_error{hr}.message().c_str());
    //    winrt::com_ptr<IDirect3DSurface9> surface{};
    //    Assert::AreEqual(MFGetService(buffer.get(), MR_BUFFER_SERVICE, IID_PPV_ARGS(surface.put())), E_NOINTERFACE);
    //}
};

class media_buffer_test_case : public TestClass<media_buffer_test_case>, public IMFMediaBuffer {
    ULONG ref_count = 1;

  private:
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) final {
        if (ppv == nullptr)
            return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, __uuidof(IUnknown)))
            *ppv = static_cast<IUnknown*>(this);
        if (IsEqualGUID(riid, __uuidof(IMFMediaBuffer)))
            *ppv = static_cast<IMFMediaBuffer*>(this);
        // is it known interface?
        if (*ppv == nullptr)
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG __stdcall AddRef() {
        return InterlockedIncrement(&ref_count);
    }

    ULONG __stdcall Release() {
        auto count = InterlockedDecrement(&ref_count);
        if (count == 0)
            spdlog::warn("{}: {}", "media_buffer_test_case", "ref_count reached 0");
        return count;
    }

    HRESULT __stdcall Lock([[maybe_unused]] BYTE** ppbBuffer, [[maybe_unused]] DWORD* pcbMaxLength,
                           [[maybe_unused]] DWORD* pcbCurrentLength) final {
        return E_NOTIMPL;
    }

    HRESULT __stdcall Unlock(void) final {
        return E_NOTIMPL;
    }

    HRESULT __stdcall GetCurrentLength(DWORD* pcbCurrentLength) override {
        if (pcbCurrentLength)
            *pcbCurrentLength = 0;
        return S_OK;
    }

    HRESULT __stdcall SetCurrentLength([[maybe_unused]] DWORD cbCurrentLength) override {
        return S_OK;
    }

    HRESULT __stdcall GetMaxLength(DWORD* pcbMaxLength) override {
        if (pcbMaxLength)
            *pcbMaxLength = 0;
        return S_OK;
    }

  public:
    ~media_buffer_test_case() noexcept = default;
    TEST_METHOD_INITIALIZE(setup) {
        Assert::AreEqual<ULONG>(1, ref_count);
    }
    TEST_METHOD_CLEANUP(teardown) {
        // ...
    }

    TEST_METHOD(test_buffer_wrapper) {
        // create a wrapper for `this`. ref_count increased
        winrt::com_ptr<IMFMediaBuffer> buf0{};
        Assert::AreEqual(MFCreateMediaBufferWrapper(this, 0, 0, buf0.put()), S_OK);
        Assert::AreEqual<ULONG>(2, ref_count);
        // add wrapper to sample. ref_count untouched
        winrt::com_ptr<IMFSample> sample{};
        Assert::AreEqual(MFCreateSample(sample.put()), S_OK);
        Assert::AreEqual(sample->AddBuffer(buf0.get()), S_OK);
        Assert::AreEqual<ULONG>(2, ref_count);
    }
};

class video_reader_test_case : public TestClass<video_reader_test_case> {
    winrt::com_ptr<IMFMediaSourceEx> source{};
    winrt::com_ptr<IMFMediaType> native_type{};
    winrt::com_ptr<IMFMediaType> source_type{};
    winrt::com_ptr<IMFSourceReaderEx> reader{}; // expose IMFTransform for each stream

  public:
    ~video_reader_test_case() noexcept = default;
    TEST_METHOD_INITIALIZE(setup) {
        Assert::AreEqual(open("test-sample-0.mp4"), S_OK);
    }
    TEST_METHOD_CLEANUP(teardown) {
        Assert::AreEqual(source->Shutdown(), S_OK);
    }

  public:
    HRESULT open(IMFActivate* device) {
        if (auto hr = device->ActivateObject(__uuidof(IMFMediaSourceEx), source.put_void()); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), nullptr, source_reader.put()); FAILED(hr))
            return hr;
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            return hr;
        auto video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        if (auto hr = reader->GetNativeMediaType(video_stream, 0, native_type.put()); FAILED(hr))
            return hr;
        source_type = native_type;
        return S_OK;
    }

    HRESULT open(std::filesystem::path p) noexcept(false) {
        MF_OBJECT_TYPE source_object_type = MF_OBJECT_INVALID;
        if (auto hr = resolve(p.generic_wstring(), source.put(), source_object_type); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFAttributes> attrs{};
        if (auto hr = MFCreateAttributes(attrs.put(), 2); FAILED(hr))
            return hr;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        winrt::com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), attrs.get(), source_reader.put()); FAILED(hr))
            return hr;
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            return hr;
        auto video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        if (auto hr = reader->GetNativeMediaType(video_stream, 0, native_type.put()); FAILED(hr))
            return hr;
        source_type = native_type;
        return S_OK;
    }

    HRESULT set_subtype(const GUID& subtype) noexcept {
        auto video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        source_type = nullptr;
        if (auto hr = reader->GetCurrentMediaType(video_stream, source_type.put()); FAILED(hr))
            return hr;
        if (auto hr = source_type->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
            return hr;
        return reader->SetCurrentMediaType(video_stream, nullptr, source_type.get());
    }

  public:
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/directx-surface-buffer
    TEST_METHOD(test_reader_h264_rgb32) {
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }

    TEST_METHOD(test_reader_h264_nv12) {
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_NV12), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }

    TEST_METHOD(test_reader_h264_i420) {
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_I420), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }

    static void consume_samples0(DWORD istream, DWORD ostream, winrt::com_ptr<IMFSourceReaderEx> source_reader,
                                 winrt::com_ptr<IMFTransform> transform, //
                                 winrt::com_ptr<IMFSample> output_sample) {
        DWORD status = 0;
        Assert::AreEqual(transform->GetInputStatus(istream, &status), S_OK);
        Assert::AreEqual<DWORD>(status, MFT_INPUT_STATUS_ACCEPT_DATA);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), S_OK);

        size_t output_count = 0;
        for (auto input_sample : read_samples(source_reader, static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM))) {
            if (auto hr = transform->ProcessInput(istream, input_sample.get(), 0); FAILED(hr)) {
                spdlog::warn("failed: {}", "ProcessInput");
                return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
            }

            MFT_OUTPUT_DATA_BUFFER output{};
            output.dwStreamID = ostream;
            output.pSample = output_sample.get();
            switch (auto hr = transform->ProcessOutput(0, 1, &output, &status); hr) {
            case S_OK:
                ++output_count;
                continue;
            case MF_E_TRANSFORM_NEED_MORE_INPUT:
                continue;
            case MF_E_TRANSFORM_STREAM_CHANGE:
                spdlog::debug("stream changed: {:#08x}", status);
                [[fallthrough]];
            default:
                spdlog::warn("failed: {}", "ProcessOutput");
                return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
            }
        }
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL), S_OK);
        Assert::AreNotEqual<size_t>(output_count, 0);
        output_count = 0;
        bool output_available = true;
        while (output_available) {
            MFT_OUTPUT_DATA_BUFFER output{};
            output.dwStreamID = ostream;
            output.pSample = output_sample.get();
            switch (auto hr = transform->ProcessOutput(0, 1, &output, &status); hr) {
            case S_OK:
                ++output_count;
                continue;
            case MF_E_TRANSFORM_NEED_MORE_INPUT:
                output_available = false;
                continue;
            case MF_E_TRANSFORM_STREAM_CHANGE:
                spdlog::debug("stream changed: {:#08x}", status);
                [[fallthrough]];
            default:
                spdlog::warn("failed: {}", "ProcessOutput");
                return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
            }
        }
        // some transforms may not process after drain.
        // in the case, output_count == 0.
    };

    /// @brief Consume function for `IMFTransform` that doesn't have leftover after drain
    /// @see CLSID_CResizerDMO
    /// @see CLSID_VideoProcessorMFT
    void consume_samples1(DWORD istream, DWORD ostream, winrt::com_ptr<IMFSourceReaderEx> source_reader,
                          winrt::com_ptr<IMFTransform> transform, //
                          winrt::com_ptr<IMFSample> output_sample) {
        size_t input_count = 0;
        size_t output_count = 0;
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), S_OK);
        DWORD status = 0;
        for (auto input_sample : read_samples(source_reader, static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM))) {
            ++input_count;
            if (auto hr = transform->ProcessInput(istream, input_sample.get(), 0); FAILED(hr)) {
                spdlog::warn("failed: {}", "ProcessInput");
                return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
            }
            bool output_available = true;
            while (output_available) {
                MFT_OUTPUT_DATA_BUFFER output{};
                output.dwStreamID = ostream;
                output.pSample = output_sample.get();
                switch (auto hr = transform->ProcessOutput(0, 1, &output, &status); hr) {
                case S_OK:
                    ++output_count;
                    continue;
                case MF_E_TRANSFORM_NEED_MORE_INPUT:
                    output_available = false;
                    continue;
                case MF_E_TRANSFORM_STREAM_CHANGE:
                    [[fallthrough]];
                default:
                    spdlog::warn("failed: {}", "ProcessOutput");
                    return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
                }
            }
        }
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL), S_OK);
        // CLSID_CResizerDMO won't have leftover
        // CLSID_VideoProcessorMFT won't have leftover
        Assert::IsTrue(output_count);
        Assert::IsTrue(input_count == output_count);
    };

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
    TEST_METHOD(test_CMSH264DecoderMFT_RGB32) {
        h264_decoder_t decoder{};
        Assert::IsTrue(decoder.support(source_type.get()));

        winrt::com_ptr<IMFTransform> transform = decoder.transform;
        // Valid configuration order can be I->O or O->I.
        // `CLSID_CMSH264DecoderMFT` uses I->O ordering
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        const DWORD istream = info.input_stream_ids[0];
        const DWORD ostream = info.output_stream_ids[0];
        winrt::com_ptr<IMFMediaType> input = source_type;
        Assert::AreEqual(transform->SetInputType(istream, input.get(), 0), S_OK);

        winrt::com_ptr<IMFMediaType> output = make_video_type(input.get(), MFVideoFormat_RGB32);
        Assert::AreEqual(transform->SetOutputType(ostream, output.get(), 0), MF_E_INVALIDMEDIATYPE);
        // we can't consume the samples because there is no transform
    }

    // for Asynchronous MFT
    // @todo https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#get-buffer-requirements
    // @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
    TEST_METHOD(test_CMSH264DecoderMFT_NV12) {
        h264_decoder_t decoder{};
        Assert::IsTrue(decoder.support(source_type.get()));

        winrt::com_ptr<IMFTransform> transform = decoder.transform;
        // Valid configuration order can be I->O or O->I.
        // `CLSID_CMSH264DecoderMFT` uses I->O ordering
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        const DWORD istream = info.input_stream_ids[0];
        const DWORD ostream = info.output_stream_ids[0];
        winrt::com_ptr<IMFMediaType> input = source_type;
        Assert::AreEqual(transform->SetInputType(istream, input.get(), 0), S_OK);
        auto output_type = make_video_type(input.get(), MFVideoFormat_NV12);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_CMSH264DecoderMFT_I420) {
        h264_decoder_t decoder{};
        Assert::IsTrue(decoder.support(source_type.get()));

        winrt::com_ptr<IMFTransform> transform = decoder.transform;
        // Valid configuration order can be I->O or O->I.
        // `CLSID_CMSH264DecoderMFT` uses I->O ordering
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        const DWORD istream = info.input_stream_ids[0];
        const DWORD ostream = info.output_stream_ids[0];
        winrt::com_ptr<IMFMediaType> input = source_type;
        Assert::AreEqual(transform->SetInputType(istream, input.get(), 0), S_OK);
        auto output_type = make_video_type(input.get(), MFVideoFormat_I420);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
    TEST_METHOD(test_CColorConvertDMO_RGB32_I420) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        // Valid configuration order can be I->O or O->I.
        // `CLSID_CColorConvertDMO` uses I->O ordering
        // CLSID_CResizerDMO won't have leftover
        color_converter_t converter{};
        winrt::com_ptr<IMFTransform> transform = converter.transform;

        const DWORD istream = 0;
        const DWORD ostream = 0;
        mf_transform_info_t info{};
        Assert::AreEqual(transform->SetInputType(istream, source_type.get(), 0), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_I420);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_CColorConvertDMO_RGB32_IYUV) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        color_converter_t converter{};
        winrt::com_ptr<IMFTransform> transform = converter.transform;

        const DWORD istream = 0;
        const DWORD ostream = 0;
        mf_transform_info_t info{};
        Assert::AreEqual(transform->SetInputType(istream, source_type.get(), 0), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_IYUV);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    /// @todo Try with Texture2D buffer
    TEST_METHOD(test_CColorConvertDMO_NV12_RGB32) {
        Assert::AreEqual(set_subtype(MFVideoFormat_NV12), S_OK);
        color_converter_t converter{};
        winrt::com_ptr<IMFTransform> transform = converter.transform;

        const DWORD istream = 0;
        const DWORD ostream = 0;
        mf_transform_info_t info{};
        Assert::AreEqual(transform->SetInputType(istream, source_type.get(), 0), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    /// @todo Try with Texture2D buffer
    TEST_METHOD(test_CColorConvertDMO_I420_RGB32) {
        Assert::AreEqual(set_subtype(MFVideoFormat_I420), S_OK);
        color_converter_t converter{};
        winrt::com_ptr<IMFTransform> transform = converter.transform;

        const DWORD istream = 0;
        const DWORD ostream = 0;
        mf_transform_info_t info{};
        Assert::AreEqual(transform->SetInputType(istream, source_type.get(), 0), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_CColorConvertDMO_I420_RGB565) {
        Assert::AreEqual(set_subtype(MFVideoFormat_I420), S_OK);
        color_converter_t converter{};
        winrt::com_ptr<IMFTransform> transform = converter.transform;

        const DWORD istream = 0;
        const DWORD ostream = 0;
        mf_transform_info_t info{};
        Assert::AreEqual(transform->SetInputType(istream, source_type.get(), 0), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB565);
        Assert::AreEqual(transform->SetOutputType(ostream, output_type.get(), 0), S_OK);

        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        consume_samples0(istream, ostream, reader, transform, output_sample);
    }

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
    TEST_METHOD(test_CResizerDMO_stream_count) {
        sample_cropper_t resizer{};
        winrt::com_ptr<IMFTransform> transform = resizer.transform;
        DWORD num_input = 0;
        DWORD num_output = 0;
        Assert::AreEqual(transform->GetStreamCount(&num_input, &num_output), S_OK);
        Assert::AreEqual<DWORD>(num_input, 1);
        Assert::AreEqual<DWORD>(num_output, 1);
    }

    TEST_METHOD(test_CResizerDMO_NV12) {
        Assert::AreEqual(set_subtype(MFVideoFormat_NV12), S_OK);
        sample_cropper_t resizer{};
        winrt::com_ptr<IMFTransform> transform = resizer.transform;
        constexpr auto istream = 0;
        constexpr DWORD flags = 0;
        Assert::AreNotEqual(transform->SetInputType(istream, source_type.get(), flags), S_OK);
    }

    TEST_METHOD(test_CResizerDMO_RGB32) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);

        sample_cropper_t resizer{};
        RECT src{0, 0, 640, 480}, dst{};
        Assert::AreEqual(resizer.crop(source_type.get(), src), S_OK);
        Assert::AreEqual(resizer.get_crop_region(src, dst), S_OK);
        Assert::AreEqual<LONG>(dst.right, 640);
        Assert::AreEqual<LONG>(dst.bottom, 480);

        winrt::com_ptr<IMFTransform> transform = resizer.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_CResizerDMO_I420) {
        Assert::AreEqual(set_subtype(MFVideoFormat_I420), S_OK);

        sample_cropper_t resizer{};
        RECT src{0, 0, 640, 480}, dst{};
        Assert::AreEqual(resizer.crop(source_type.get(), src), S_OK);
        Assert::AreEqual(resizer.get_crop_region(src, dst), S_OK);
        Assert::AreEqual<LONG>(dst.right, 640);
        Assert::AreEqual<LONG>(dst.bottom, 480);

        winrt::com_ptr<IMFTransform> transform = resizer.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());
        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);
        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/video-processor-mft#remarks
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
    TEST_METHOD(test_VideoProcessorMFT_stream_count) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);

        sample_processor_t processor{};
        winrt::com_ptr<IMFTransform> transform = processor.transform;

        DWORD num_input = 0;
        DWORD num_output = 0;
        Assert::AreEqual(transform->GetStreamCount(&num_input, &num_output), S_OK);
        Assert::AreEqual<DWORD>(num_input, 1);
        Assert::AreEqual<DWORD>(num_output, 1);
    }

    TEST_METHOD(test_VideoProcessorMFT_horizontal_normal) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);

        sample_processor_t processor{};
        Assert::AreEqual(processor.set_type(source_type.get(), output_type.get()), S_OK);
        Assert::AreEqual(processor.set_size(RECT{0, 0, 1280, 720}), S_OK);
        // letterboxes the output as needed
        Assert::AreEqual(processor.set_mirror_rotation(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_HORIZONTAL,
                                                       MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL),
                         S_OK);
        MFARGB color{};
        Assert::AreEqual(processor.control->SetBorderColor(&color), S_OK);

        winrt::com_ptr<IMFTransform> transform = processor.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_VideoProcessorMFT_vertical_normal) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);

        sample_processor_t processor{};
        Assert::AreEqual(processor.set_type(source_type.get(), output_type.get()), S_OK);
        Assert::AreEqual(processor.set_size(RECT{0, 0, 1280, 720}), S_OK);
        Assert::AreEqual(processor.set_mirror_rotation(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_VERTICAL,
                                                       MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL),
                         S_OK);
        MFARGB color{};
        Assert::AreEqual(processor.control->SetBorderColor(&color), S_OK);

        winrt::com_ptr<IMFTransform> transform = processor.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_VideoProcessorMFT_scale_0) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);

        sample_processor_t processor{};
        Assert::AreEqual(MFSetAttributeSize(output_type.get(), MF_MT_FRAME_SIZE, 720, 720), S_OK);
        Assert::AreEqual(processor.set_type(source_type.get(), output_type.get()), S_OK);
        MFARGB color{};
        Assert::AreEqual(processor.control->SetBorderColor(&color), S_OK);

        winrt::com_ptr<IMFTransform> transform = processor.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }

    TEST_METHOD(test_VideoProcessorMFT_scale_1) {
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        auto output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);

        sample_processor_t processor{};
        Assert::AreEqual(processor.set_scale(source_type.get(), 720, 720), S_OK);
        MFARGB color{};
        Assert::AreEqual(processor.control->SetBorderColor(&color), S_OK);

        winrt::com_ptr<IMFTransform> transform = processor.transform;
        mf_transform_info_t info{};
        info.from(transform.get());
        Assert::IsFalse(info.output_provide_sample());

        winrt::com_ptr<IMFSample> output_sample{};
        Assert::AreEqual(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize), S_OK);

        constexpr auto istream = 0;
        constexpr auto ostream = 0;
        consume_samples1(istream, ostream, reader, transform, output_sample);
    }
};

class rgba32_texture_buffer_test_case : public TestClass<rgba32_texture_buffer_test_case> {
    winrt::com_ptr<ID3D11Device> device{};
    winrt::com_ptr<IMFMediaSourceEx> source{};
    winrt::com_ptr<IMFMediaType> source_type{};
    winrt::com_ptr<IMFSourceReaderEx> reader{}; // expose IMFTransform for each stream
    const DWORD reader_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    winrt::com_ptr<ID3D11Texture2D> texture{};
    winrt::com_ptr<IMFSample> sample{};
    winrt::com_ptr<IMFMediaBuffer> buffer{};
    winrt::com_ptr<IMF2DBuffer2> buf2d{};

  public:
    ~rgba32_texture_buffer_test_case() noexcept = default;
    TEST_METHOD_INITIALIZE(setup) {
        D3D_FEATURE_LEVEL device_feature_level{};
        winrt::com_ptr<ID3D11DeviceContext> device_context{};
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                                            D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                        levels, 2, D3D11_SDK_VERSION, device.put(), &device_feature_level,
                                        device_context.put());
            FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        if (auto hr = make_texture(device.get(), texture.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        if (auto hr = make_texture_surface(texture.get(), sample.put()); FAILED(hr))
            Assert::Fail(winrt::hresult_error{hr}.message().c_str());
        Assert::AreEqual(sample->GetBufferByIndex(0, buffer.put()), S_OK);
        Assert::AreEqual(buffer->QueryInterface(buf2d.put()), S_OK);
    }

    HRESULT open(std::filesystem::path p) noexcept(false) {
        MF_OBJECT_TYPE source_object_type = MF_OBJECT_INVALID;
        if (auto hr = resolve(p.generic_wstring(), source.put(), source_object_type); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFAttributes> attrs{};
        if (auto hr = MFCreateAttributes(attrs.put(), 2); FAILED(hr))
            return hr;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        winrt::com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), attrs.get(), source_reader.put()); FAILED(hr))
            return hr;
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFMediaType> native_type{};
        auto video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        if (auto hr = reader->GetNativeMediaType(video_stream, 0, native_type.put()); FAILED(hr))
            return hr;
        source_type = native_type;
        return S_OK;
    }

    HRESULT set_subtype(const GUID& subtype) noexcept {
        auto video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        source_type = nullptr;
        if (auto hr = reader->GetCurrentMediaType(video_stream, source_type.put()); FAILED(hr))
            return hr;
        if (auto hr = source_type->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
            return hr;
        return reader->SetCurrentMediaType(video_stream, nullptr, source_type.get());
    }

    void consume_samples(winrt::com_ptr<IMFSourceReaderEx> source_reader, //
                         winrt::com_ptr<IMFTransform> transform,          //
                         const mf_transform_info_t& info,                 //
                         winrt::com_ptr<IMFSample> output_sample) {
        const DWORD istream = info.input_stream_ids[0];
        const DWORD ostream = info.output_stream_ids[0];
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (auto input_sample : read_samples(source_reader, static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM))) {
            if (auto hr = transform->ProcessInput(istream, input_sample.get(), 0); FAILED(hr)) {
                spdlog::warn("failed: {}", "ProcessInput");
                return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
            }
            bool output_available = true;
            while (output_available) {
                MFT_OUTPUT_DATA_BUFFER output{};
                output.dwStreamID = ostream;
                output.pSample = output_sample.get();
                switch (auto hr = transform->ProcessOutput(0, 1, &output, &status); hr) {
                case S_OK:
                    ++output_count;
                    continue;
                case MF_E_TRANSFORM_NEED_MORE_INPUT:
                    output_available = false;
                    continue;
                case MF_E_TRANSFORM_STREAM_CHANGE:
                    [[fallthrough]];
                default:
                    spdlog::warn("failed: {}", "ProcessOutput");
                    return Assert::Fail(winrt::hresult_error{hr}.message().c_str());
                }
            }
        }
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL), S_OK);
        Assert::AreEqual(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL), S_OK);
        Assert::IsTrue(output_count);
    };

    TEST_METHOD(test_crop) {
        Assert::AreEqual(open("test-sample-0.mp4"), S_OK);
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        const RECT dst{0, 0, 256, 256};

        sample_cropper_t cropper{};
        Assert::AreEqual(cropper.crop(source_type.get(), dst), S_OK);

        mf_transform_info_t info{};
        info.from(cropper.transform.get());
        Assert::IsFalse(info.output_provide_sample());
        Assert::AreEqual<DWORD>(info.output_info.cbSize, dst.right * dst.bottom * 4);

        consume_samples(reader, cropper.transform, info, sample);
    }

    TEST_METHOD(test_downscale) {
        Assert::AreEqual(open("test-sample-0.mp4"), S_OK);
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        const RECT dst{0, 0, 256, 256};

        sample_processor_t resizer{};
        Assert::AreEqual(resizer.set_scale(source_type.get(), dst.right, dst.bottom), S_OK);

        mf_transform_info_t info{};
        info.from(resizer.transform.get());
        Assert::IsFalse(info.output_provide_sample());
        Assert::AreEqual<DWORD>(info.output_info.cbSize, dst.right * dst.bottom * 4);

        consume_samples(reader, resizer.transform, info, sample);
    }
};
