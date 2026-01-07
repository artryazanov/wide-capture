#pragma once
#include "Encoder.h"
#include <mutex>

#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}
#pragma warning(pop)

namespace Video {
    class FFmpegBackend : public Encoder {
    public:
        FFmpegBackend();
        ~FFmpegBackend();

        bool Initialize(ID3D11Device* pDevice, int width, int height, int fps, const std::string& filename) override;
        void EncodeFrame(ID3D11Texture2D* pSourceTexture) override;
        void Finish() override;

    private:
        void InitHWContext(ID3D11Device* pDevice);

        AVFormatContext* m_fmtCtx = nullptr;
        AVCodecContext* m_codecCtx = nullptr;
        AVStream* m_videoStream = nullptr;
        
        AVBufferRef* m_hwDeviceRef = nullptr;
        AVBufferRef* m_hwFramesRef = nullptr;
        
        std::mutex m_mutex;
        int64_t m_pts = 0;
        int m_width = 0;
        int m_height = 0;
    };
}
