#include <catch2/catch.hpp>

// clang-format off
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d9.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windowsx.h>

#include <codecapi.h>
#include <dxva2api.h>
#include <mediaobj.h>
#include <wmcodecdsp.h>
// clang-format on
#include <filesystem>
#include <spdlog/spdlog.h>
#include <winrt/Windows.Foundation.h>

namespace fs = std::filesystem;

using winrt::com_ptr;

fs::path get_asset_dir() noexcept;

struct video_transform_test_case {
    com_ptr<IMFMediaSourceEx> source{};
    com_ptr<IMFMediaType> source_type{};
    com_ptr<IMFSourceReaderEx> reader{}; // expose IMFTransform for each stream
    com_ptr<IMFSourceReaderCallback> reader_callback{};
    DWORD reader_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

  public:
    video_transform_test_case() {
        open(get_asset_dir() / "test-sample-0.mp4");
    }
    ~video_transform_test_case() {
        const auto hr = source->Shutdown();
        if (SUCCEEDED(hr))
            return;
        report_error(hr, __func__);
    }

  public:
    void open(IMFActivate* device) {
        if (auto hr = device->ActivateObject(__uuidof(IMFMediaSourceEx), source.put_void()); FAILED(hr))
            winrt::throw_hresult(hr);
        com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), nullptr, source_reader.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = reader->GetNativeMediaType(reader_stream, 0, source_type.put()); FAILED(hr))
            winrt::throw_hresult(hr);
    }

    void open(fs::path p) noexcept(false) {
        MF_OBJECT_TYPE source_object_type = MF_OBJECT_INVALID;
        if (auto hr = resolve(p.generic_wstring(), source.put(), source_object_type); FAILED(hr))
            winrt::throw_hresult(hr);
        com_ptr<IMFAttributes> attrs{};
        if (auto hr = MFCreateAttributes(attrs.put(), 2); FAILED(hr))
            winrt::throw_hresult(hr);
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
        com_ptr<IMFSourceReader> source_reader{};
        if (auto hr = MFCreateSourceReaderFromMediaSource(source.get(), attrs.get(), source_reader.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = source_reader->QueryInterface(reader.put()); FAILED(hr))
            winrt::throw_hresult(hr);
        if (auto hr = reader->GetNativeMediaType(reader_stream, 0, source_type.put()); FAILED(hr))
            winrt::throw_hresult(hr);
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
    static void report_error(HRESULT hr, const char* fname) noexcept {
        winrt::hresult_error ex{hr};
        spdlog::error("{}: {:#08x} {}", fname, static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }

    static HRESULT make_transform_video(IMFTransform** transform, const IID& iid) noexcept {
        com_ptr<IUnknown> unknown{};
        if (auto hr = CoCreateInstance(iid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
            return hr;
        return unknown->QueryInterface(transform);
    }

    // @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder#transform-attributes
    static HRESULT configure_acceleration_H264(IMFTransform* transform) noexcept {
        com_ptr<IMFAttributes> attrs{};
        if (auto hr = transform->GetAttributes(attrs.put()); FAILED(hr))
            return hr;
        if (auto hr = attrs->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, TRUE); FAILED(hr))
            spdlog::error("CODECAPI_AVDecVideoAcceleration_H264: {:#08x}", hr);
        if (auto hr = attrs->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE); FAILED(hr))
            spdlog::error("CODECAPI_AVLowLatencyMode: {:#08x}", hr);
        if (auto hr = attrs->SetUINT32(CODECAPI_AVDecNumWorkerThreads, 1); FAILED(hr))
            spdlog::error("CODECAPI_AVDecNumWorkerThreads: {:#08x}", hr);
        return S_OK;
    }

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

    static HRESULT make_video_type(IMFMediaType** ptr, const GUID& subtype) noexcept {
        com_ptr<IMFMediaType> type{};
        if (auto hr = MFCreateMediaType(type.put()); FAILED(hr))
            return hr;
        if (auto hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); FAILED(hr))
            return hr;
        if (auto hr = type->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
            return hr;
        *ptr = type.get();
        type->AddRef();
        return S_OK;
    }

    static com_ptr<IMFMediaType> make_output_type(com_ptr<IMFMediaType> input, const GUID& subtype) {
        com_ptr<IMFMediaType> output{};
        REQUIRE(make_video_type(output.put(), subtype) == S_OK);
        REQUIRE(input->CopyAllItems(output.get()) == S_OK);
        //if (IsEqualGUID(subtype, MFVideoFormat_RGB32))
        //    REQUIRE(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Unknown) == S_OK);
        //UINT32 w = 0, h = 0;
        //REQUIRE(MFGetAttributeSize(input.get(), MF_MT_FRAME_SIZE, &w, &h) == S_OK);
        //REQUIRE(MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, w, h) == S_OK);
        //UINT32 num = 0, denom = 1;
        //REQUIRE(MFGetAttributeRatio(input.get(), MF_MT_FRAME_RATE, &num, &denom) == S_OK);
        //REQUIRE(MFSetAttributeSize(output.get(), MF_MT_FRAME_RATE, num, denom) == S_OK);
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

    /// @todo use GPU buffer
    static HRESULT get_transform_output(IMFTransform* transform, DWORD ostream, IMFSample** sample, GUID& subtype,
                                        BOOL& flushed) {
        MFT_OUTPUT_STREAM_INFO stream_info{};
        if (auto hr = transform->GetOutputStreamInfo(ostream, &stream_info); FAILED(hr))
            return hr;

        flushed = FALSE;
        *sample = nullptr;

        MFT_OUTPUT_DATA_BUFFER output{};
        if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
            if (auto hr = create_single_buffer_sample(sample, stream_info.cbSize); FAILED(hr))
                return hr;
            output.pSample = *sample;
        }

        DWORD status = 0;
        HRESULT const result = transform->ProcessOutput(0, 1, &output, &status);
        if (result == S_OK) {
            *sample = output.pSample;
            return S_OK;
        }

        /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/handling-stream-changes
        if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
            com_ptr<IMFMediaType> changed_output_type{};
            if (output.dwStatus != MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE) {
                // todo: add more works for this case
                return E_NOTIMPL;
            }

            // query the output type and its subtype
            if (auto hr = transform->GetOutputAvailableType(ostream, 0, changed_output_type.put()); FAILED(hr))
                return hr;
            // check new output media type
            if (auto hr = changed_output_type->GetGUID(MF_MT_SUBTYPE, &subtype); FAILED(hr))
                return hr;

            if (auto hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL); FAILED(hr))
                return hr;
            flushed = TRUE;
            return S_OK;
        }
        // MF_E_TRANSFORM_NEED_MORE_INPUT: not an error condition but it means the allocated output sample is empty.
        return result;
    }

    static HRESULT consume(com_ptr<IMFSourceReader> source_reader, DWORD istream) {
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
        }
        return S_OK;
    }

    static void consume(com_ptr<IMFSourceReader> source_reader, com_ptr<IMFTransform> transform, //
                        DWORD istream, DWORD ostream) {
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
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
            if (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                input_available = false;
                continue;
            }
            // probably MF_SOURCE_READERF_STREAMTICK
            if (input_sample == nullptr)
                continue;
            input_sample->SetSampleTime(sample_timestamp);
            switch (auto hr = transform->ProcessInput(istream, input_sample.get(), 0)) {
            case S_OK: // MF_E_TRANSFORM_TYPE_NOT_SET, MF_E_NO_SAMPLE_DURATION, MF_E_NO_SAMPLE_TIMESTAMP
                break;
            case MF_E_NOTACCEPTING:
            case MF_E_UNSUPPORTED_D3D_TYPE:
            case E_INVALIDARG:
            default:
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
            while (true) {
                BOOL flushed = FALSE;
                com_ptr<IMFSample> sample{};
                GUID subtype{};
                auto hr = get_transform_output(transform.get(), ostream, sample.put(), subtype, flushed);
                if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                    break;
                if (FAILED(hr)) {
                    report_error(hr, __func__);
                    FAIL(static_cast<uint32_t>(hr));
                }
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // fetch remaining output in the transform
        while (true) {
            BOOL flushed = FALSE;
            com_ptr<IMFSample> sample{};
            GUID subtype{};
            HRESULT hr = get_transform_output(transform.get(), ostream, sample.put(), subtype, flushed);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                break;
            if (FAILED(hr)) {
                report_error(hr, __func__);
                FAIL(static_cast<uint32_t>(hr));
            }
            // processed output
        }
    }

    static HRESULT configure_rectangle(IMFVideoProcessorControl* control, IMFMediaType* media_type) noexcept {
        UINT32 w = 0, h = 0;
        if (auto hr = MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &w, &h); FAILED(hr))
            return hr;
        RECT rect{};
        rect.right = w; // LTRB rectangle
        rect.bottom = h;
        if (auto hr = control->SetSourceRectangle(&rect); FAILED(hr))
            return hr;
        return control->SetDestinationRectangle(&rect);
    }

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

TEST_CASE_METHOD(video_transform_test_case, "IMFSourceReader - H264", "[codec]") {
    GUID subtype{};
    REQUIRE(source_type->GetGUID(MF_MT_SUBTYPE, &subtype) == S_OK);
    REQUIRE(IsEqualGUID(subtype, MFVideoFormat_H264));

    constexpr DWORD istream = 0;
    SECTION("RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(consume(reader, istream) == S_OK);
    }
    SECTION("NV12") {
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(consume(reader, istream) == S_OK);
    }
    SECTION("I420") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(consume(reader, istream) == S_OK);
    };
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_transform_test_case, "MFTransform - CLSID_CMSH264DecoderMFT", "[codec]") {
    REQUIRE(set_subtype(MFVideoFormat_H264) == S_OK);

    com_ptr<IMFTransform> transform{};
    REQUIRE(make_transform_video(transform.put(), CLSID_CMSH264DecoderMFT) == S_OK);
    REQUIRE(configure_acceleration_H264(transform.get()) == S_OK);

    // Valid configuration order can be I->O or O->I.
    // `CLSID_CMSH264DecoderMFT` uses I->O ordering
    DWORD num_input = 0;
    DWORD num_output = 0;
    REQUIRE(transform->GetStreamCount(&num_input, &num_output) == S_OK);
    const DWORD istream = num_input - 1;
    const DWORD ostream = num_output - 1;

    SECTION("RGB32") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output{};
        REQUIRE(MFCreateMediaType(output.put()) == S_OK);
        REQUIRE(input->CopyAllItems(output.get()) == S_OK);
        REQUIRE(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output.get(), 0) == MF_E_INVALIDMEDIATYPE);
        // we can't consume the samples because there is no transform
    }
    SECTION("NV12") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output{};
        REQUIRE(MFCreateMediaType(output.put()) == S_OK);
        REQUIRE(input->CopyAllItems(output.get()) == S_OK);
        REQUIRE(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output.get(), 0) == S_OK);

        DWORD status = 0;
        REQUIRE(transform->GetInputStatus(istream, &status) == S_OK);
        REQUIRE(status == MFT_INPUT_STATUS_ACCEPT_DATA);
        // for Asynchronous MFT
        // @todo https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#get-buffer-requirements
        // @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        consume(reader, transform, istream, ostream);
    }
    SECTION("IYUV") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output{};
        REQUIRE(MFCreateMediaType(output.put()) == S_OK);
        REQUIRE(input->CopyAllItems(output.get()) == S_OK);
        REQUIRE(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_IYUV) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output.get(), 0) == S_OK);

        DWORD status = 0;
        REQUIRE(transform->GetInputStatus(istream, &status) == S_OK);
        REQUIRE(status == MFT_INPUT_STATUS_ACCEPT_DATA);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        consume(reader, transform, istream, ostream);
    }
    SECTION("I420") {
        com_ptr<IMFMediaType> input = source_type;
        REQUIRE(transform->SetInputType(istream, input.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output{};
        REQUIRE(MFCreateMediaType(output.put()) == S_OK);
        REQUIRE(input->CopyAllItems(output.get()) == S_OK);
        REQUIRE(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetOutputType(ostream, output.get(), 0) == S_OK);

        DWORD status = 0;
        REQUIRE(transform->GetInputStatus(istream, &status) == S_OK);
        REQUIRE(status == MFT_INPUT_STATUS_ACCEPT_DATA);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        consume(reader, transform, istream, ostream);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/colorconverter
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_transform_test_case, "MFTransform - CLSID_CColorConvertDMO", "[dsp]") {
    com_ptr<IMFTransform> transform{};
    REQUIRE(make_transform_video(transform.put(), CLSID_CColorConvertDMO) == S_OK);
    com_ptr<IPropertyStore> props{};
    {
        REQUIRE(transform->QueryInterface(props.put()) == S_OK);
        PROPVARIANT var{};
        REQUIRE(SUCCEEDED(props->GetValue(MFPKEY_COLORCONV_MODE, &var)));
        // spdlog::debug("- MFPKEY_COLORCONV_MODE: {}", var.intVal == 0 ? "Progressive" : "Interlaced");
        // Microsoft DirectX Media Object https://docs.microsoft.com/en-us/previous-versions/windows/desktop/api/mediaobj/nn-mediaobj-imediaobject
        com_ptr<IMediaObject> media_object{};
        REQUIRE(transform->QueryInterface(media_object.put()) == S_OK);
    }

    com_ptr<IMFSourceReaderEx> source_reader = reader;
    DWORD istream = 0;
    DWORD ostream = 0;
    SECTION("RGB32 - I420") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_I420);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(source_reader, transform, istream, ostream);
    }
    SECTION("RGB32 - IYUV") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_IYUV);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(source_reader, transform, istream, ostream);
    }
    SECTION("NV12 - RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(source_reader, transform, istream, ostream);
    }
    SECTION("I420 - RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(source_reader, transform, istream, ostream);
    }
    SECTION("I420 - RGB565") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);

        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB565);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(source_reader, transform, istream, ostream);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/videoresizer
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_transform_test_case, "MFTransform - Video Resizer DSP", "[dsp]") {
    com_ptr<IMFTransform> transform{};
    REQUIRE(make_transform_video(transform.put(), CLSID_CResizerDMO) == S_OK);
    com_ptr<IWMResizerProps> resizer{};
    REQUIRE(transform->QueryInterface(resizer.put()) == S_OK);

    DWORD num_input = 0;
    DWORD num_output = 0;
    REQUIRE(transform->GetStreamCount(&num_input, &num_output) == S_OK);
    const DWORD istream = num_input - 1;
    const DWORD ostream = num_output - 1;

    SECTION("RGB32") {
        REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        UINT32 width = 0, height = 0;
        MFGetAttributeSize(source_type.get(), MF_MT_FRAME_SIZE, &width, &height);
        REQUIRE(resizer->SetClipRegion(0, 0, width / 2, height / 2) == S_OK);
        REQUIRE(MFSetAttributeSize(output_type.get(), MF_MT_FRAME_SIZE, width / 2, height / 2) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(reader, transform, istream, ostream);
    }
    SECTION("I420") {
        REQUIRE(set_subtype(MFVideoFormat_I420) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_I420);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        UINT32 width = 0, height = 0;
        MFGetAttributeSize(source_type.get(), MF_MT_FRAME_SIZE, &width, &height);
        REQUIRE(resizer->SetClipRegion(0, 0, width / 2, height / 2) == S_OK);
        REQUIRE(MFSetAttributeSize(output_type.get(), MF_MT_FRAME_SIZE, width / 2, height / 2) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_CResizerDMO won't have leftover
        consume(reader, transform, istream, ostream);
    }
    SECTION("NV12") {
        REQUIRE(set_subtype(MFVideoFormat_NV12) == S_OK);
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) != S_OK);
    }
}

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/video-processor-mft#remarks
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
TEST_CASE_METHOD(video_transform_test_case, "MFTransform - Video Processor MFT", "[dsp]") {
    REQUIRE(set_subtype(MFVideoFormat_RGB32) == S_OK);

    com_ptr<IMFTransform> transform{};
    REQUIRE(make_transform_video(transform.put(), CLSID_VideoProcessorMFT) == S_OK);

    DWORD num_input = 0;
    DWORD num_output = 0;
    REQUIRE(transform->GetStreamCount(&num_input, &num_output) == S_OK);
    const DWORD istream = num_input - 1;
    const DWORD ostream = num_output - 1;

    // https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-work-queue-and-threading-improvements
    com_ptr<IMFRealTimeClientEx> realtime{};
    REQUIRE(transform->QueryInterface(realtime.put()) == S_OK);

    com_ptr<IMFVideoProcessorControl> control{};
    REQUIRE(transform->QueryInterface(control.put()) == S_OK);

    SECTION("MIRROR_HORIZONTAL/ROTAION_NORMAL") {
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(configure_rectangle(control.get(), source_type.get()) == S_OK);
        // H mirror, corrects the orientation, letterboxes the output as needed
        REQUIRE(control->SetMirror(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_HORIZONTAL) == S_OK);
        REQUIRE(control->SetRotation(MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL) == S_OK);
        MFARGB color{};
        REQUIRE(control->SetBorderColor(&color) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_VideoProcessorMFT won't have leftover
        consume(reader, transform, istream, ostream);
    }
    SECTION("MIRROR_VERTICAL/ROTAION_NORMAL") {
        REQUIRE(transform->SetInputType(istream, source_type.get(), 0) == S_OK);
        com_ptr<IMFMediaType> output_type = make_output_type(source_type, MFVideoFormat_RGB32);
        REQUIRE(transform->SetOutputType(ostream, output_type.get(), 0) == S_OK);

        REQUIRE(configure_rectangle(control.get(), source_type.get()) == S_OK);
        // H mirror, corrects the orientation, letterboxes the output as needed
        REQUIRE(control->SetMirror(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_VERTICAL) == S_OK);
        REQUIRE(control->SetRotation(MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL) == S_OK);
        MFARGB color{};
        REQUIRE(control->SetBorderColor(&color) == S_OK);

        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL) == S_OK);
        // CLSID_VideoProcessorMFT won't have leftover
        consume(reader, transform, istream, ostream);
    }
}
