#include <fmt/format.h>
#include <mfapi.h>
#include <mferror.h>
#include <spdlog/spdlog.h>
#include <system_error>

std::string w2mb(std::wstring_view in) noexcept(false) {
    std::string out{};
    out.reserve(MB_CUR_MAX * in.length());
    mbstate_t state{};
    for (wchar_t wc : in) {
        size_t len = 0;
        char mb[8]{}; // ensure null-terminated for UTF-8 (maximum 4 byte)
        if (auto ec = wcrtomb_s(&len, mb, wc, &state))
            throw std::system_error{ec, std::system_category(), "wcrtomb_s"};
        out += std::string_view{mb, len};
    }
    return out;
}

std::wstring mb2w(std::string_view in) noexcept(false) {
    std::wstring out{};
    out.reserve(in.length());
    const char* ptr = in.data();
    const char* const end = in.data() + in.length();
    mbstate_t state{};
    wchar_t wc{};
    while (size_t len = mbrtowc(&wc, ptr, end - ptr, &state)) {
        if (len == static_cast<size_t>(-1)) // bad encoding
            throw std::system_error{errno, std::system_category(), "mbrtowc"};
        if (len == static_cast<size_t>(-2)) // valid but incomplete
            break;                          // nothing to do more
        out.push_back(wc);
        ptr += len; // advance [1...n]
    }
    return out;
}

std::string to_hex_string(HRESULT hr) noexcept {
    return fmt::format("{:#08x}", static_cast<uint32_t>(hr));
}

std::string to_guid_string(const GUID& guid) noexcept {
    constexpr auto bufsz = 40;
    wchar_t buf[bufsz]{};
    size_t buflen = StringFromGUID2(guid, buf, bufsz);
    return w2mb({buf + 1, buflen - 3}); // GUID requires 36 characters
}

/// @see https://docs.microsoft.com/en-us/windows/win32/wmformat/media-type-identifiers
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-type-debugging-code
std::string to_mf_string(const GUID& guid) noexcept {
#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val)                                                                                    \
    if (val == param)                                                                                                  \
    return #val
#endif
    IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_SUBTYPE);
    IF_EQUAL_RETURN(guid, MF_MT_ALL_SAMPLES_INDEPENDENT);
    IF_EQUAL_RETURN(guid, MF_MT_FIXED_SIZE_SAMPLES);
    IF_EQUAL_RETURN(guid, MF_MT_COMPRESSED);
    IF_EQUAL_RETURN(guid, MF_MT_SAMPLE_SIZE);
    IF_EQUAL_RETURN(guid, MF_MT_WRAPPED_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_NUM_CHANNELS);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BLOCK_ALIGNMENT);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BITS_PER_SAMPLE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_VALID_BITS_PER_SAMPLE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_BLOCK);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_CHANNEL_MASK);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FOLDDOWN_MATRIX);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKREF);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKTARGET);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGREF);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGTARGET);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_PREFER_WAVEFORMATEX);
    IF_EQUAL_RETURN(guid, MF_MT_AAC_PAYLOAD_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_SIZE);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MAX);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MIN);
    IF_EQUAL_RETURN(guid, MF_MT_PIXEL_ASPECT_RATIO);
    IF_EQUAL_RETURN(guid, MF_MT_DRM_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_PAD_CONTROL_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_SOURCE_CONTENT_HINT);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_CHROMA_SITING);
    IF_EQUAL_RETURN(guid, MF_MT_INTERLACE_MODE);
    IF_EQUAL_RETURN(guid, MF_MT_TRANSFER_FUNCTION);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_PRIMARIES);
    IF_EQUAL_RETURN(guid, MF_MT_CUSTOM_VIDEO_PRIMARIES);
    IF_EQUAL_RETURN(guid, MF_MT_YUV_MATRIX);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_LIGHTING);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_NOMINAL_RANGE);
    IF_EQUAL_RETURN(guid, MF_MT_GEOMETRIC_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_MINIMUM_DISPLAY_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_ENABLED);
    IF_EQUAL_RETURN(guid, MF_MT_AVG_BITRATE);
    IF_EQUAL_RETURN(guid, MF_MT_AVG_BIT_ERROR_RATE);
    IF_EQUAL_RETURN(guid, MF_MT_MAX_KEYFRAME_SPACING);
    IF_EQUAL_RETURN(guid, MF_MT_DEFAULT_STRIDE);
    IF_EQUAL_RETURN(guid, MF_MT_PALETTE);
    IF_EQUAL_RETURN(guid, MF_MT_USER_DATA);
    IF_EQUAL_RETURN(guid, MF_MT_AM_FORMAT_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG_START_TIME_CODE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_PROFILE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_LEVEL);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG_SEQUENCE_HEADER);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_0);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_0);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_1);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_1);
    IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_SRC_PACK);
    IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_CTRL_PACK);
    IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_HEADER);
    IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_FORMAT);
    IF_EQUAL_RETURN(guid, MF_MT_IMAGE_LOSS_TOLERANT);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG4_SAMPLE_DESCRIPTION);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY);
    IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_4CC);
    IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_WAVE_FORMAT_TAG);

    // MF_MT_MAJOR_TYPE
    IF_EQUAL_RETURN(guid, MFMediaType_Audio);
    IF_EQUAL_RETURN(guid, MFMediaType_Video);
    IF_EQUAL_RETURN(guid, MFMediaType_Protected);
    IF_EQUAL_RETURN(guid, MFMediaType_SAMI);
    IF_EQUAL_RETURN(guid, MFMediaType_Script);
    IF_EQUAL_RETURN(guid, MFMediaType_Image);
    IF_EQUAL_RETURN(guid, MFMediaType_HTML);
    IF_EQUAL_RETURN(guid, MFMediaType_Binary);
    IF_EQUAL_RETURN(guid, MFMediaType_FileTransfer);

    // subtype
    IF_EQUAL_RETURN(guid, MFVideoFormat_AI44);    // FCC('AI44')
    IF_EQUAL_RETURN(guid, MFVideoFormat_ARGB32);  // D3DFMT_A8R8G8B8
    IF_EQUAL_RETURN(guid, MFVideoFormat_AYUV);    // FCC('AYUV')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DV25);    // FCC('dv25')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DV50);    // FCC('dv50')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVH1);    // FCC('dvh1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVSD);    // FCC('dvsd')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVSL);    // FCC('dvsl')
    IF_EQUAL_RETURN(guid, MFVideoFormat_H264);    // FCC('H264')
    IF_EQUAL_RETURN(guid, MFVideoFormat_H264_ES); //
    IF_EQUAL_RETURN(guid, MFVideoFormat_I420);    // FCC('I420')
    IF_EQUAL_RETURN(guid, MFVideoFormat_IYUV);    // FCC('IYUV')
    IF_EQUAL_RETURN(guid, MFVideoFormat_M4S2);    // FCC('M4S2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MJPG);
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP43);   // FCC('MP43')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP4S);   // FCC('MP4S')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP4V);   // FCC('MP4V')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MPG1);   // FCC('MPG1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MSS1);   // FCC('MSS1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MSS2);   // FCC('MSS2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_NV11);   // FCC('NV11')
    IF_EQUAL_RETURN(guid, MFVideoFormat_NV12);   // FCC('NV12')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P010);   // FCC('P010')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P016);   // FCC('P016')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P210);   // FCC('P210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P216);   // FCC('P216')
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB24);  // D3DFMT_R8G8B8
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB32);  // D3DFMT_X8R8G8B8
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB555); // D3DFMT_X1R5G5B5
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB565); // D3DFMT_R5G6B5
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB8);
    IF_EQUAL_RETURN(guid, MFVideoFormat_UYVY); // FCC('UYVY')
    IF_EQUAL_RETURN(guid, MFVideoFormat_v210); // FCC('v210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_v410); // FCC('v410')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV1); // FCC('WMV1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV2); // FCC('WMV2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV3); // FCC('WMV3')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WVC1); // FCC('WVC1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y210); // FCC('Y210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y216); // FCC('Y216')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y410); // FCC('Y410')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y416); // FCC('Y416')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y41P);
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y41T);
    IF_EQUAL_RETURN(guid, MFVideoFormat_YUY2); // FCC('YUY2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_YV12); // FCC('YV12')
    IF_EQUAL_RETURN(guid, MFVideoFormat_YVYU);

    IF_EQUAL_RETURN(guid, MFAudioFormat_PCM);              // WAVE_FORMAT_PCM
    IF_EQUAL_RETURN(guid, MFAudioFormat_Float);            // WAVE_FORMAT_IEEE_FLOAT
    IF_EQUAL_RETURN(guid, MFAudioFormat_DTS);              // WAVE_FORMAT_DTS
    IF_EQUAL_RETURN(guid, MFAudioFormat_Dolby_AC3_SPDIF);  // WAVE_FORMAT_DOLBY_AC3_SPDIF
    IF_EQUAL_RETURN(guid, MFAudioFormat_DRM);              // WAVE_FORMAT_DRM
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV8);        // WAVE_FORMAT_WMAUDIO2
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV9);        // WAVE_FORMAT_WMAUDIO3
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudio_Lossless); // WAVE_FORMAT_WMAUDIO_LOSSLESS
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMASPDIF);         // WAVE_FORMAT_WMASPDIF
    IF_EQUAL_RETURN(guid, MFAudioFormat_MSP1);             // WAVE_FORMAT_WMAVOICE9
    IF_EQUAL_RETURN(guid, MFAudioFormat_MP3);              // WAVE_FORMAT_MPEGLAYER3
    IF_EQUAL_RETURN(guid, MFAudioFormat_MPEG);             // WAVE_FORMAT_MPEG
    IF_EQUAL_RETURN(guid, MFAudioFormat_AAC);              // WAVE_FORMAT_MPEG_HEAAC
    IF_EQUAL_RETURN(guid, MFAudioFormat_ADTS);             // WAVE_FORMAT_MPEG_ADTS_AAC
#undef IF_EQUAL_RETURN
    return to_guid_string(guid);

    // MFVideoFormat_RGB32 // 444 (32 bpp)
    // MFVideoFormat_ARGB32
    // MFVideoFormat_RGB24
    // MFVideoFormat_I420 // 420 (16 bpp)
    // MFVideoFormat_NV12 // 420 (12 bpp)
    // MFVideoFormat_UYVY // 422 (12 bpp)
    // MFVideoFormat_MJPG
    // MFVideoFormat_AI44 // 4:4:4 Packed P
    // MFVideoFormat_AYUV // 4:4:4 Packed 8
    // MFVideoFormat_I420 // 4:2:0 Planar 8
    // MFVideoFormat_IYUV // 4:2:0 Planar 8
    // MFVideoFormat_NV11 // 4:1:1 Planar 8
    // MFVideoFormat_NV12 // 4:2:0 Planar 8
    // MFVideoFormat_UYVY // 4:2:2 Packed 8
    // MFVideoFormat_Y41P // 4:1:1 Packed 8
    // MFVideoFormat_Y41T // 4:1:1 Packed 8
    // MFVideoFormat_Y42T // 4:2:2 Packed 8
    // MFVideoFormat_YUY2 // 4:2:2 Packed 8
    // MFVideoFormat_YVU9 // 8:4:4 Planar 9
    // MFVideoFormat_YV12 // 4:2:0 Planar 8
    // MFVideoFormat_YVYU // 4:2:2 Packed 8
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
