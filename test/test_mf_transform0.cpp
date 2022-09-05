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

struct video_buffer_test_case : public TestClass<video_buffer_test_case> {
    winrt::com_ptr<ID3D11Device> device{};
    D3D_FEATURE_LEVEL device_feature_level{};
    winrt::com_ptr<ID3D11DeviceContext> device_context{};

  public:
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
    /// @see https://docs.microsoft.com/en-us/windows/win32/api/evr/nc-evr-mfcreatevideosamplefromsurface
    static HRESULT make_texture_surface(ID3D11Texture2D* tex2d, IMFSample** ptr) {
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

struct video_reader_test_case : public TestClass<video_reader_test_case> {
    winrt::com_ptr<IMFMediaSourceEx> source{};
    winrt::com_ptr<IMFMediaType> native_type{};
    winrt::com_ptr<IMFMediaType> source_type{};
    winrt::com_ptr<IMFSourceReaderEx> reader{}; // expose IMFTransform for each stream
    const DWORD reader_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

  public:
    TEST_METHOD_INITIALIZE(setup) {
        // ...
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
        if (auto hr = reader->GetNativeMediaType(reader_stream, 0, native_type.put()); FAILED(hr))
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
        if (auto hr = reader->GetNativeMediaType(reader_stream, 0, native_type.put()); FAILED(hr))
            return hr;
        source_type = native_type;
        return S_OK;
    }

    HRESULT set_subtype(const GUID& subtype) noexcept {
        source_type = nullptr;
        if (auto hr = reader->GetCurrentMediaType(reader_stream, source_type.put()); FAILED(hr))
            return hr;
        if (auto hr = source_type->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
            return hr;
        return reader->SetCurrentMediaType(reader_stream, nullptr, source_type.get());
    }

  public:
    static HRESULT resolve(const std::wstring& fpath, IMFMediaSourceEx** source,
                           MF_OBJECT_TYPE& media_object_type) noexcept {
        winrt::com_ptr<IMFSourceResolver> resolver{};
        if (auto hr = MFCreateSourceResolver(resolver.put()); FAILED(hr))
            return hr;
        winrt::com_ptr<IUnknown> unknown{};
        if (auto hr = resolver->CreateObjectFromURL(fpath.c_str(), MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_READ,
                                                    nullptr, &media_object_type, unknown.put());
            FAILED(hr))
            return hr;
        return unknown->QueryInterface(source);
    }

    static HRESULT consume(winrt::com_ptr<IMFSourceReaderEx> source_reader, DWORD istream, size_t& count) {
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

    static auto read_samples(winrt::com_ptr<IMFSourceReaderEx> reader, DWORD stream_index)
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

    static winrt::com_ptr<IMFMediaType> clone(IMFMediaType* input) noexcept(false) {
        winrt::com_ptr<IMFMediaType> output{};
        if (auto hr = MFCreateMediaType(output.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = input->CopyAllItems(output.get()); FAILED(hr))
            winrt::throw_hresult(hr);
        return output;
    }

    static winrt::com_ptr<IMFMediaType> make_video_type(IMFMediaType* input, const GUID& subtype) noexcept {
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
    static HRESULT create_single_buffer_sample(IMFSample** output, DWORD bufsz) {
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

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/directx-surface-buffer
    TEST_METHOD(test_reader_h264_rgb32) {
        open("test-sample-0.mp4");
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_RGB32), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }

    TEST_METHOD(test_reader_h264_nv12) {
        open("test-sample-0.mp4");
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_NV12), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }

    TEST_METHOD(test_reader_h264_i420) {
        open("test-sample-0.mp4");
        GUID subtype{};
        Assert::AreEqual(source_type->GetGUID(MF_MT_SUBTYPE, &subtype), S_OK);
        Assert::IsTrue(IsEqualGUID(subtype, MFVideoFormat_H264));
        Assert::AreEqual(set_subtype(MFVideoFormat_I420), S_OK);
        size_t num_frame = 0;
        constexpr DWORD istream = 0;
        Assert::AreEqual(consume(reader, istream, num_frame), S_OK);
    }
};
