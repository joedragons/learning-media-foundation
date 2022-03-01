#include <catch2/catch.hpp>

// clang-format OFF
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d9.h>
#include <evr.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windowsx.h>

#include <codecapi.h>
#include <dxva2api.h>
#include <mediaobj.h>
#include <mmdeviceapi.h>
#include <wmcodecdsp.h>
// clang-format on
#include <experimental/generator>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <winrt/Windows.Foundation.h>

namespace fs = std::filesystem;

using winrt::com_ptr;

fs::path get_asset_dir() noexcept;

void report_error(HRESULT hr, const char* fname) noexcept {
    winrt::hresult_error ex{hr};
    spdlog::error("{}: {:#08x} {}", fname, static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
}
std::string to_mf_string(const GUID& guid) noexcept;

struct video_buffer_test_case {
    com_ptr<ID3D11Device> device{};
    D3D_FEATURE_LEVEL device_feature_level{};
    com_ptr<ID3D11DeviceContext> device_context{};

  public:
    video_buffer_test_case() {
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                                            D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                        levels, 2, D3D11_SDK_VERSION, device.put(), &device_feature_level,
                                        device_context.put());
            FAILED(hr))
            FAIL(hr);
        com_ptr<ID3D10Multithread> threading{};
        if (auto hr = device->QueryInterface(threading.put()); SUCCEEDED(hr))
            threading->SetMultithreadProtected(true);
    }

  public:
    HRESULT make_texture(ID3D11Texture2D** texture) {
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

    /// @see https://docs.microsoft.com/en-us/windows/win32/api/evr/nc-evr-mfcreatevideosamplefromsurface
    static HRESULT make_texture_surface(ID3D11Texture2D* tex2d, IMFSample** ptr) {
        // Create a DXGI media buffer by calling the MFCreateDXGISurfaceBuffer function.
        // Pass in the ID3D11Texture2D pointer and the offset for each element in the texture array.
        // The function returns an IMFMediaBuffer pointer.
        com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d, 0, false, buffer.put()); FAILED(hr))
            return hr;

        // Create an empty media sample by calling the MFCreateVideoSampleFromSurface function.
        // Set the pUnkSurface parameter equal to NULL. The function returns an IMFSample pointer.
        com_ptr<IMFSample> sample{};
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
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/uncompressed-video-buffers
TEST_CASE_METHOD(video_buffer_test_case, "Uncompressed Video Buffer") {
    com_ptr<ID3D11Texture2D> tex2d{};
    if (auto hr = make_texture(tex2d.put()); FAILED(hr))
        FAIL(hr);

    com_ptr<IMFMediaBuffer> buffer{};
    if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d.get(), 0, false, buffer.put()); FAILED(hr))
        FAIL(hr);

    SECTION("IMFDXGIBuffer") {
        com_ptr<IMFDXGIBuffer> dxgi{};
        REQUIRE(buffer->QueryInterface(dxgi.put()) == S_OK);
        com_ptr<ID3D11Texture2D> texture{}; // should be same with `tex2d` above
        REQUIRE(dxgi->GetResource(IID_PPV_ARGS(texture.put())) == S_OK);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        REQUIRE(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    SECTION("IMF2DBuffer2") {
        com_ptr<IMF2DBuffer2> buf2d{};
        REQUIRE(buffer->QueryInterface(buf2d.put()) == S_OK);
    }
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/directx-surface-buffer
    SECTION("IDirect3DSurface9") {
        com_ptr<IDirect3DSurface9> surface{};
        REQUIRE(MFGetService(buffer.get(), MR_BUFFER_SERVICE, IID_PPV_ARGS(surface.put())) == E_NOINTERFACE);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/video-subtype-guids
/// @see https://stackoverflow.com/a/9681384
void print(IMFMediaType* media_type) noexcept {
    GUID major{};
    media_type->GetGUID(MF_MT_MAJOR_TYPE, &major);
    spdlog::info("media_type:");
    spdlog::info("  {}: {}", "major", to_mf_string(major));

    if (major == MFMediaType_Audio) {
        GUID subtype{};
        if SUCCEEDED (media_type->GetGUID(MF_MT_SUBTYPE, &subtype))
            spdlog::info("  {}: {}", "subtype", to_mf_string(subtype));
        return;
    }
    if (major == MFMediaType_Video) {
        GUID subtype{};
        if SUCCEEDED (media_type->GetGUID(MF_MT_SUBTYPE, &subtype))
            spdlog::info("  {}: {}", "subtype", to_mf_string(subtype));

        UINT32 value = FALSE;
        if SUCCEEDED (media_type->GetUINT32(MF_MT_COMPRESSED, &value))
            spdlog::info("  {}: {}", "compressed", static_cast<bool>(value));
        if SUCCEEDED (media_type->GetUINT32(MF_MT_FIXED_SIZE_SAMPLES, &value))
            spdlog::debug("  {}: {}", "fixed_size", static_cast<bool>(value));
        if SUCCEEDED (media_type->GetUINT32(MF_MT_AVG_BITRATE, &value))
            spdlog::debug("  {}: {}", "bitrate", value);
        if SUCCEEDED (media_type->GetUINT32(MF_MT_INTERLACE_MODE, &value)) {
            switch (value) {
            case MFVideoInterlace_MixedInterlaceOrProgressive:
                spdlog::debug("  {}: {}", "interlace", "MixedInterlaceOrProgressive");
                break;
            case MFVideoInterlace_Progressive:
                spdlog::debug("  {}: {}", "interlace", "Progressive");
                break;
            case MFVideoInterlace_Unknown:
                [[fallthrough]];
            default:
                spdlog::debug("  {}: {}", "interlace", "Unknown");
                break;
            }
        }

        UINT32 num = 0, denom = 1;
        if SUCCEEDED (MFGetAttributeRatio(media_type, MF_MT_FRAME_RATE, &num, &denom))
            spdlog::info("  {}: {:.1f}", "fps", static_cast<float>(num) / denom);
        if SUCCEEDED (MFGetAttributeRatio(media_type, MF_MT_PIXEL_ASPECT_RATIO, &num, &denom))
            spdlog::info("  {}: {:.3f}", "aspect_ratio", static_cast<float>(num) / denom);

        UINT32 w = 0, h = 0;
        if SUCCEEDED (MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &w, &h)) {
            spdlog::info("  {}: {}", "width", w);
            spdlog::info("  {}: {}", "height", h);
        }
    }
}

struct video_reader_test_case {
    com_ptr<IMFMediaSourceEx> source{};
    com_ptr<IMFMediaType> native_type{};
    com_ptr<IMFMediaType> source_type{};
    com_ptr<IMFSourceReaderEx> reader{}; // expose IMFTransform for each stream
    const DWORD reader_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

  public:
    video_reader_test_case() {
        open(get_asset_dir() / "test-sample-0.mp4");
        print(native_type.get());
    }
    ~video_reader_test_case() {
        if (auto hr = source->Shutdown(); FAILED(hr))
            report_error(hr, __func__);
    }

  public:
    HRESULT open(IMFActivate* device) {
        if (auto hr = device->ActivateObject(__uuidof(IMFMediaSourceEx), source.put_void()); FAILED(hr))
            return hr;
        com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), nullptr, source_reader.put()); FAILED(hr))
            return hr;
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            return hr;
        if (auto hr = reader->GetNativeMediaType(reader_stream, 0, native_type.put()); FAILED(hr))
            return hr;
        source_type = native_type;
        return S_OK;
    }

    HRESULT open(fs::path p) noexcept(false) {
        MF_OBJECT_TYPE source_object_type = MF_OBJECT_INVALID;
        if (auto hr = resolve(p.generic_wstring(), source.put(), source_object_type); FAILED(hr))
            return hr;
        com_ptr<IMFAttributes> attrs{};
        if (auto hr = MFCreateAttributes(attrs.put(), 2); FAILED(hr))
            return hr;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        com_ptr<IMFSourceReader> source_reader{};
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
        com_ptr<IMFSourceResolver> resolver{};
        if (auto hr = MFCreateSourceResolver(resolver.put()); FAILED(hr))
            return hr;
        com_ptr<IUnknown> unknown{};
        if (auto hr = resolver->CreateObjectFromURL(fpath.c_str(), MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_READ,
                                                    nullptr, &media_object_type, unknown.put());
            FAILED(hr))
            return hr;
        return unknown->QueryInterface(source);
    }

    static HRESULT consume(com_ptr<IMFSourceReaderEx> source_reader, DWORD istream, size_t& count) {
        bool input_available = true;
        while (input_available) {
            DWORD stream_index{};
            DWORD sample_flags{};
            LONGLONG sample_timestamp = 0; // unit 100-nanosecond
            com_ptr<IMFSample> input_sample{};
            if (auto hr = source_reader->ReadSample(istream, 0, &stream_index, &sample_flags, &sample_timestamp,
                                                    input_sample.put());
                FAILED(hr)) {
                CAPTURE(sample_flags);
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

    static auto read_samples(com_ptr<IMFSourceReaderEx> reader, DWORD stream_index)
        -> std::experimental::generator<com_ptr<IMFSample>> {
        while (true) {
            DWORD actual_index{};
            DWORD flags{};
            LONGLONG timestamp = 0; // unit 100-nanosecond
            com_ptr<IMFSample> sample{};
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

    static com_ptr<IMFMediaType> clone(IMFMediaType* input) noexcept(false) {
        com_ptr<IMFMediaType> output{};
        if (auto hr = MFCreateMediaType(output.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = input->CopyAllItems(output.get()); FAILED(hr))
            winrt::throw_hresult(hr);
        return output;
    }

    static com_ptr<IMFMediaType> make_video_type(IMFMediaType* input, const GUID& subtype) noexcept {
        com_ptr<IMFMediaType> output = clone(input);
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
        com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateMemoryBuffer(bufsz, buffer.put()); FAILED(hr))
            return hr;
        // GetMaxLength will be length of the available memory location
        // GetCurrentLength will be 0
        IMFSample* sample = *output;
        return sample->AddBuffer(buffer.get());
    }
};

TEST_CASE_METHOD(video_reader_test_case, "IMFSourceReader - H264", "[codec]") {
    GUID subtype{};
    REQUIRE(source_type->GetGUID(MF_MT_SUBTYPE, &subtype) == S_OK);
    REQUIRE(IsEqualGUID(subtype, MFVideoFormat_H264));

    size_t num_frame = 0;
    constexpr DWORD istream = 0;
    SECTION("RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(consume(reader, istream, num_frame) == S_OK);
        spdlog::debug("sample count: {}", num_frame);
    }
    SECTION("NV12") {
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(consume(reader, istream, num_frame) == S_OK);
    }
    SECTION("I420") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(consume(reader, istream, num_frame) == S_OK);
    };
}

struct mf_transform_info_t {
    DWORD num_input = 0;
    DWORD num_output = 0;
    DWORD input_stream_ids[1]{};
    DWORD output_stream_ids[1]{};
    MFT_INPUT_STREAM_INFO input_info{};
    MFT_OUTPUT_STREAM_INFO output_info{};

  public:
    /// @todo check flags related to sample/buffer constraint
    explicit mf_transform_info_t(IMFTransform* transform) noexcept(false) {
        if (auto hr = transform->GetStreamCount(&num_input, &num_output); FAILED(hr))
            winrt::throw_hresult(hr);
        switch (auto hr = transform->GetStreamIDs(1, input_stream_ids, 1, output_stream_ids)) {
        case S_OK:
        case E_NOTIMPL:
            break; // some transform might not implement this.
        default:
            report_error(hr, "GetStreamIDs");
        }
        // CLSID_CColorConvertDMO requires the I/O type configured
        if (auto hr = transform->GetInputStreamInfo(input_stream_ids[0], &input_info); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = transform->GetOutputStreamInfo(output_stream_ids[0], &output_info); FAILED(hr))
            winrt::throw_hresult(hr);
    }

    /// @see MFT_OUTPUT_STREAM_PROVIDES_SAMPLES
    /// @see MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES
    bool output_provide_sample() const noexcept {
        bool flag0 = output_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
        bool flag1 = output_info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES;
        return flag0 || flag1;
    }
};

/**
 * @brief `IMFTransform` owner for `MFVideoFormat_H264`
 * @todo Support `MFVideoFormat_H264_ES`, `MFVideoFormat_H264_HDCP`
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
 */
struct h264_decoder_t {
    com_ptr<IMFTransform> transform{};

  public:
    explicit h264_decoder_t(const GUID& clsid = CLSID_CMSH264DecoderMFT) noexcept(false) {
        com_ptr<IUnknown> unknown{};
        if (auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        configure_acceleration_H264(transform.get());
    }

    bool support(IMFMediaType* source_type) const noexcept {
        GUID subtype{};
        if FAILED (source_type->GetGUID(MF_MT_SUBTYPE, &subtype))
            return false;
        return IsEqualGUID(subtype, MFVideoFormat_H264);
    }

  public:
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder#transform-attributes
    static void configure_acceleration_H264(IMFTransform* transform) {
        com_ptr<IMFAttributes> attrs{};
        if (auto hr = transform->GetAttributes(attrs.put()); FAILED(hr))
            return spdlog::error("{}: {:#08x}", "Failed to get IMFAttributes of the IMFTransform", hr);
        if (auto hr = attrs->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, TRUE); FAILED(hr))
            spdlog::error("{}: {:#08x}", "CODECAPI_AVDecVideoAcceleration_H264", hr);
        if (auto hr = attrs->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE); FAILED(hr))
            spdlog::error("{}: {:#08x}", "CODECAPI_AVLowLatencyMode", hr);
        if (auto hr = attrs->SetUINT32(CODECAPI_AVDecNumWorkerThreads, 1); FAILED(hr))
            spdlog::error("{}: {:#08x}", "CODECAPI_AVDecNumWorkerThreads", hr);
    }
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_reader_test_case, "MFTransform - CLSID_CMSH264DecoderMFT", "[codec]") {
    h264_decoder_t decoder{CLSID_CMSH264DecoderMFT};
    REQUIRE(decoder.support(source_type.get()));

    com_ptr<IMFTransform> transform = decoder.transform;
    // Valid configuration order can be I->O or O->I.
    // `CLSID_CMSH264DecoderMFT` uses I->O ordering
    mf_transform_info_t info{transform.get()};
    REQUIRE_FALSE(info.output_provide_sample());

    com_ptr<IMFSample> output_sample{};
    REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

    const auto istream = info.input_stream_ids[0];
    const auto ostream = info.output_stream_ids[0];
    SECTION("RGB32") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output = make_video_type(input.get(), MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output.get(), 0) == MF_E_INVALIDMEDIATYPE);
        // we can't consume the samples because there is no transform
    }

    // for Asynchronous MFT
    // @todo https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#get-buffer-requirements
    // @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
    SECTION("NV12") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(input.get(), MFVideoFormat_NV12);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        DWORD status = 0;
        REQUIRE(transform->GetInputStatus(istream, &status) == S_OK);
        REQUIRE(status == MFT_INPUT_STATUS_ACCEPT_DATA);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);

        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
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
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
        }
        REQUIRE(output_count);
    }
    SECTION("I420") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(input.get(), MFVideoFormat_I420);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        DWORD status = 0;
        REQUIRE(transform->GetInputStatus(istream, &status) == S_OK);
        REQUIRE(status == MFT_INPUT_STATUS_ACCEPT_DATA);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);

        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
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
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
        }
        REQUIRE(output_count);
    }
    // todo: MFVideoFormat_IYUV
}

/// @see CLSID_CColorConvertDMO
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/colorconverter
/// @see Microsoft DirectX Media Object https://docs.microsoft.com/en-us/previous-versions/windows/desktop/api/mediaobj/nn-mediaobj-imediaobject
struct color_converter_t {
    com_ptr<IMFTransform> transform{};
    com_ptr<IPropertyStore> props{};
    com_ptr<IMediaObject> media_object{};

  public:
    explicit color_converter_t(const GUID& clsid = CLSID_CColorConvertDMO) noexcept(false) {
        com_ptr<IUnknown> unknown{};
        if (auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        winrt::check_hresult(transform->QueryInterface(props.put()));
        winrt::check_hresult(transform->QueryInterface(media_object.put()));
    }
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_reader_test_case, "MFTransform - CLSID_CColorConvertDMO", "[dsp]") {
    // Valid configuration order can be I->O or O->I.
    // `CLSID_CColorConvertDMO` uses I->O ordering
    // CLSID_CResizerDMO won't have leftover
    color_converter_t converter{CLSID_CColorConvertDMO};
    com_ptr<IMFTransform> transform = converter.transform;

    const auto istream = 0;
    const auto ostream = 0;
    SECTION("RGB32 - I420") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_I420);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
    }
    SECTION("RGB32 - IYUV") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_IYUV);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
    }
    SECTION("NV12 - RGB32") {
        // @todo Try with Texture2D buffer
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
    }
    SECTION("I420 - RGB32") {
        // @todo Try with Texture2D buffer
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
    }
    SECTION("I420 - RGB565") {
        // @todo Try with Texture2D buffer
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB565);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        size_t output_count = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        REQUIRE(output_count);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/videoresizer
struct sample_resizer_t {
    com_ptr<IMFTransform> transform{};
    com_ptr<IWMResizerProps> props{};

  public:
    sample_resizer_t() {
        com_ptr<IUnknown> unknown{};
        if (auto hr = CoCreateInstance(CLSID_CResizerDMO, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        winrt::check_hresult(transform->QueryInterface(props.put()));
    }

  public:
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/videoresizer
    static HRESULT configure_source_rectangle(IPropertyStore* props, const RECT& rect) noexcept {
        PROPVARIANT val{};
        val.intVal = rect.left;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_SRC_LEFT, val); FAILED(hr))
            return hr;
        val.intVal = rect.top;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_SRC_TOP, val); FAILED(hr))
            return hr;
        val.intVal = rect.right - rect.left;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_SRC_WIDTH, val); FAILED(hr))
            return hr;
        val.intVal = rect.bottom - rect.top;
        return props->SetValue(MFPKEY_RESIZE_SRC_HEIGHT, val);
    }

    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/videoresizer
    static HRESULT configure_destination_rectangle(IPropertyStore* props, const RECT& rect) noexcept {
        PROPVARIANT val{};
        val.intVal = rect.left;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_DST_LEFT, val); FAILED(hr))
            return hr;
        val.intVal = rect.top;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_DST_TOP, val); FAILED(hr))
            return hr;
        val.intVal = rect.right - rect.left;
        if (auto hr = props->SetValue(MFPKEY_RESIZE_DST_WIDTH, val); FAILED(hr))
            return hr;
        val.intVal = rect.bottom - rect.top;
        return props->SetValue(MFPKEY_RESIZE_DST_HEIGHT, val);
    }
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_reader_test_case, "MFTransform - Video Resizer DSP", "[dsp]") {
    sample_resizer_t resizer{};
    com_ptr<IMFTransform> transform = resizer.transform;
    com_ptr<IWMResizerProps> props = resizer.props;

    DWORD num_input = 0;
    DWORD num_output = 0;
    REQUIRE(transform->GetStreamCount(&num_input, &num_output) == S_OK);
    REQUIRE(num_input == 1);
    REQUIRE(num_output == 1);

    const auto istream = num_input - 1;
    const auto ostream = num_input - 1;
    SECTION("NV12") {
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) != S_OK);
    }

    //UINT32 width = 0, height = 0;
    //MFGetAttributeSize(source_type.get(), MF_MT_FRAME_SIZE, &width, &height);
    SECTION("RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        REQUIRE(props->SetClipRegion(0, 0, 640, 480) == S_OK);
        REQUIRE(MFSetAttributeSize(output_type.get(), MF_MT_FRAME_SIZE, 640, 480) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        size_t input_count = 0;
        size_t output_count = 0;
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            ++input_count;
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        REQUIRE(output_count);
        REQUIRE(input_count == output_count);
    }
    SECTION("I420") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_I420);
        REQUIRE(props->SetClipRegion(0, 0, 640, 480) == S_OK);
        REQUIRE(MFSetAttributeSize(output_type.get(), MF_MT_FRAME_SIZE, 640, 480) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        size_t input_count = 0;
        size_t output_count = 0;
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            ++input_count;
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        REQUIRE(output_count);
        REQUIRE(input_count == output_count);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-work-queue-and-threading-improvements
struct sample_processor_t {
    com_ptr<IMFTransform> transform{};
    com_ptr<IMFVideoProcessorControl> control{};
    com_ptr<IMFRealTimeClientEx> realtime{};

  public:
    sample_processor_t() {
        com_ptr<IUnknown> unknown{};
        if (auto hr = CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put()));
            FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        winrt::check_hresult(transform->QueryInterface(control.put()));
        winrt::check_hresult(transform->QueryInterface(realtime.put()));
    }

  public:
    HRESULT configure_rectangle(IMFMediaType* media_type) noexcept {
        if (media_type == nullptr)
            return E_POINTER;
        return configure_rectangle(control.get(), media_type);
    }

  public:
    static HRESULT configure_rectangle(IMFVideoProcessorControl* control, IMFMediaType* media_type) noexcept {
        UINT32 w = 0, h = 0;
        if (auto hr = MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &w, &h); FAILED(hr))
            return hr;
        RECT rect{}; // LTRB rectangle
        rect.right = w;
        rect.bottom = h;
        if (auto hr = control->SetSourceRectangle(&rect); FAILED(hr))
            return hr;
        return control->SetDestinationRectangle(&rect);
    }
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/video-processor-mft#remarks
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_reader_test_case, "MFTransform - Video Processor MFT", "[dsp]") {
    REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);

    sample_processor_t processor{};
    com_ptr<IMFTransform> transform = processor.transform;

    DWORD num_input = 0;
    DWORD num_output = 0;
    REQUIRE(transform->GetStreamCount(&num_input, &num_output) == S_OK);

    const DWORD istream = num_input - 1;
    const DWORD ostream = num_output - 1;
    SECTION("MIRROR_HORIZONTAL/ROTAION_NORMAL") {
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(processor.configure_rectangle(source_type.get()) == S_OK);

        // H mirror, corrects the orientation, letterboxes the output as needed
        com_ptr<IMFVideoProcessorControl> control = processor.control;
        REQUIRE(control->SetMirror(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_HORIZONTAL) == S_OK);
        REQUIRE(control->SetRotation(MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL) == S_OK);
        MFARGB color{};
        REQUIRE(control->SetBorderColor(&color) == S_OK);

        size_t input_count = 0;
        size_t output_count = 0;
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            ++input_count;
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // CLSID_VideoProcessorMFT won't have leftover
        REQUIRE(output_count);
        REQUIRE(input_count == output_count);
    }
    SECTION("MIRROR_VERTICAL/ROTAION_NORMAL") {
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_video_type(source_type.get(), MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        mf_transform_info_t info{transform.get()};
        REQUIRE_FALSE(info.output_provide_sample());
        com_ptr<IMFSample> output_sample{};
        REQUIRE(create_single_buffer_sample(output_sample.put(), info.output_info.cbSize) == S_OK);

        REQUIRE(processor.configure_rectangle(source_type.get()) == S_OK);
        // H mirror, corrects the orientation, letterboxes the output as needed
        com_ptr<IMFVideoProcessorControl> control = processor.control;
        REQUIRE(control->SetMirror(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_VERTICAL) == S_OK);
        REQUIRE(control->SetRotation(MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL) == S_OK);
        MFARGB color{};
        REQUIRE(control->SetBorderColor(&color) == S_OK);

        size_t input_count = 0;
        size_t output_count = 0;
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        DWORD status = 0;
        for (com_ptr<IMFSample> sample : read_samples(reader, reader_stream)) {
            ++input_count;
            if (auto hr = transform->ProcessInput(istream, sample.get(), 0); FAILED(hr)) {
                report_error(hr, "ProcessInput");
                FAIL(static_cast<uint32_t>(hr));
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
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // CLSID_VideoProcessorMFT won't have leftover
        REQUIRE(output_count);
        REQUIRE(input_count == output_count);
    }
}
