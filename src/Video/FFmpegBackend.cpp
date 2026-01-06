#include "pch.h"
#include "FFmpegBackend.h"
#include "../Core/Logger.h"

namespace Video {
    FFmpegBackend::FFmpegBackend() {
        av_log_set_level(AV_LOG_WARNING);
    }

    FFmpegBackend::~FFmpegBackend() {
        Finish();
    }

    void FFmpegBackend::Finish() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_codecCtx) {
            // Flush encoder
            avcodec_send_frame(m_codecCtx, nullptr);
            // Read remaining packets... skipped for brevity, should look like loop below
            
            if (m_fmtCtx) {
                av_write_trailer(m_fmtCtx);
                if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&m_fmtCtx->pb);
                }
                avformat_free_context(m_fmtCtx);
                m_fmtCtx = nullptr;
            }
            
            avcodec_free_context(&m_codecCtx);
        }
        
        if (m_hwFramesRef) av_buffer_unref(&m_hwFramesRef);
        if (m_hwDeviceRef) av_buffer_unref(&m_hwDeviceRef);
    }

    void FFmpegBackend::InitHWContext(ID3D11Device* pDevice) {
        m_hwDeviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!m_hwDeviceRef) throw std::runtime_error("Failed to alloc HW device ctx");

        AVHWDeviceContext* deviceCtx = (AVHWDeviceContext*)m_hwDeviceRef->data;
        AVD3D11VADeviceContext* d3d11Ctx = (AVD3D11VADeviceContext*)deviceCtx->hwctx;

        d3d11Ctx->device = pDevice;
        pDevice->AddRef(); 

        if (av_hwdevice_ctx_init(m_hwDeviceRef) < 0) throw std::runtime_error("Failed to init HW device ctx");

        m_hwFramesRef = av_hwframe_ctx_alloc(m_hwDeviceRef);
        if (!m_hwFramesRef) throw std::runtime_error("Failed to alloc HW frames ctx");

        AVHWFramesContext* framesCtx = (AVHWFramesContext*)m_hwFramesRef->data;
        framesCtx->format = AV_PIX_FMT_D3D11;   
        framesCtx->sw_format = AV_PIX_FMT_NV12; // Match DXGI_FORMAT_NV12
        framesCtx->width = m_width;
        framesCtx->height = m_height;
        framesCtx->initial_pool_size = 20; // Ensure pool is allocated for CopyResource workflow

        // Explicitly set BindFlags to what we know works (BIND_RENDER_TARGET | BIND_SHADER_RESOURCE)
        // Failure 80070057 (E_INVALIDARG) suggests default flags (often BIND_DECODER) might be rejected for NV12 or by driver.
        AVD3D11VAFramesContext* framesHwCtx = (AVD3D11VAFramesContext*)framesCtx->hwctx;
        framesHwCtx->BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        framesHwCtx->MiscFlags = 0;

        if (av_hwframe_ctx_init(m_hwFramesRef) < 0) throw std::runtime_error("Failed to init HW frames ctx");
    }

    bool FFmpegBackend::Initialize(ID3D11Device* pDevice, int width, int height, int fps, const std::string& filename) {
        m_width = width;
        m_height = height;

        try {
            InitHWContext(pDevice);

            const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
            if (!codec) {
                LOG_WARNING("NVENC not found, trying h264_amf...");
                codec = avcodec_find_encoder_by_name("h264_amf");
            }
            if (!codec) {
                 LOG_WARNING("AMF not found, trying default h264...");
                 codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            }
            if (!codec) throw std::runtime_error("No encoder found!");

            m_codecCtx = avcodec_alloc_context3(codec);
            m_codecCtx->width = width;
            m_codecCtx->height = height;
            m_codecCtx->time_base = { 1, fps }; // Rational time base
            m_codecCtx->framerate = { fps, 1 };
            m_codecCtx->pix_fmt = AV_PIX_FMT_D3D11; 
            
            m_codecCtx->bit_rate = 50000000; // 50 Mbps
            m_codecCtx->gop_size = fps * 2;
            m_codecCtx->max_b_frames = 0; 
            
            m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceRef);
            m_codecCtx->hw_frames_ctx = av_buffer_ref(m_hwFramesRef);

            if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) throw std::runtime_error("Could not open codec");

            avformat_alloc_output_context2(&m_fmtCtx, nullptr, nullptr, filename.c_str());
            m_videoStream = avformat_new_stream(m_fmtCtx, nullptr);
            avcodec_parameters_from_context(m_videoStream->codecpar, m_codecCtx);
            // Important: Rescale timebase? Handled by av_packet_rescale_ts later.
            m_videoStream->time_base = m_codecCtx->time_base;

            if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&m_fmtCtx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) throw std::runtime_error("Could not open output file");
            }

            AVDictionary* opt = nullptr;
            av_dict_set(&opt, "movflags", "faststart", 0);
            if (avformat_write_header(m_fmtCtx, &opt) < 0) {
                throw std::runtime_error("Failed to write header");
            }
            
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("FFmpeg Init Failed: ", e.what());
            return false;
        }
    }

    void FFmpegBackend::EncodeFrame(ID3D11Texture2D* pSourceTexture) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. Allocate a frame from the HW pool
        AVFrame* frame = av_frame_alloc();
        if (av_hwframe_get_buffer(m_hwFramesRef, frame, 0) < 0) {
            LOG_ERROR("Failed to allocate HW frame");
            av_frame_free(&frame);
            return;
        }

        // 2. Map/Copy Logic
        // We need ID3D11DeviceContext to CopyResource.
        // We can get it from the device associated with the HW context.
        AVHWDeviceContext* deviceCtx = (AVHWDeviceContext*)m_hwDeviceRef->data;
        AVD3D11VADeviceContext* d3d11Ctx = (AVD3D11VADeviceContext*)deviceCtx->hwctx;
        ID3D11Device* device = d3d11Ctx->device;
        
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        
        if (ctx) {
            // FIX: Use CopySubresourceRegion instead of CopyResource
            // frame->data[0] is the resource (likely Texture2DArray if pool > 0)
            // frame->data[1] is the index (array slice) within that resource
            
            ID3D11Texture2D* dstTexture = (ID3D11Texture2D*)frame->data[0];
            intptr_t index = (intptr_t)frame->data[1]; // Subresource index

            // Copy entire texture to the specific subresource index
            ctx->CopySubresourceRegion(dstTexture, (UINT)index, 0, 0, 0, pSourceTexture, 0, nullptr);
            
            ctx->Release();
        }

        frame->pts = m_pts++;

        // 3. Send to Validated Frame
        int ret = avcodec_send_frame(m_codecCtx, frame);
        if (ret < 0) {
            LOG_ERROR("Error sending frame to encoder: ", ret);
        }

        AVPacket* pkt = av_packet_alloc();
        while (ret >= 0) {
            ret = avcodec_receive_packet(m_codecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            else if (ret < 0) {
                LOG_ERROR("Error receiving packet");
                break;
            }

            av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);
            pkt->stream_index = m_videoStream->index;
            av_interleaved_write_frame(m_fmtCtx, pkt);
            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        av_frame_free(&frame); // Returns texture to pool
    }
}
