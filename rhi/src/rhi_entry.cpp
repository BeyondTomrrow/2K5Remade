#include "nfl2k5_rhi.h"
#include "rhi_backend.hpp"
#include <memory>
#include <span>

/**
 * @brief Internal implementation of the opaque nfl2k5_rhi_context structure.
 */
struct nfl2k5_rhi_context {
    std::unique_ptr<nfl2k5::rhi::IRhiBackend> backend;
    nfl2k5_rhi_init_desc_t                    config{};
};

extern "C" {

nfl2k5_rhi_result_t nfl2k5_rhi_create(const nfl2k5_rhi_init_desc_t* desc, nfl2k5_rhi_t* out_ctx) {
    if (!desc || !out_ctx || !desc->native_window) {
        return NFL2K5_RHI_ERROR_INVALID_PARAM;
    }

    auto ctx = std::make_unique<nfl2k5_rhi_context>();
    ctx->config = *desc;

    // Route to the requested backend factory
    switch (desc->backend) {
    case NFL2K5_RHI_BACKEND_DX12:
        ctx->backend = nfl2k5::rhi::create_dx12_backend();
        break;
    case NFL2K5_RHI_BACKEND_VULKAN:
        ctx->backend = nfl2k5::rhi::create_vulkan_backend();
        break;
    case NFL2K5_RHI_BACKEND_AUTO:
    default:
#if defined(_WIN32)
        ctx->backend = nfl2k5::rhi::create_dx12_backend();
#else
        ctx->backend = nfl2k5::rhi::create_vulkan_backend();
#endif
        break;
    }

    if (!ctx->backend) {
        return NFL2K5_RHI_ERROR_UNSUPPORTED;
    }

    nfl2k5::rhi::Status status = ctx->backend->initialize(*desc);
    if (status != nfl2k5::rhi::Status::Success) {
        return nfl2k5::rhi::to_c_result(status);
    }

    *out_ctx = ctx.release();
    return NFL2K5_RHI_SUCCESS;
}

nfl2k5_rhi_result_t nfl2k5_rhi_submit_pushbuffer(nfl2k5_rhi_t ctx, const nfl2k5_rhi_pushbuffer_desc_t* desc) {
    if (!ctx || !ctx->backend || !desc) {
        return NFL2K5_RHI_ERROR_INVALID_PARAM;
    }

    std::span<const uint32_t> word_span(desc->words, desc->count);
    nfl2k5::rhi::Status status = ctx->backend->submit_pushbuffer(word_span, desc->guest_va);
    return nfl2k5::rhi::to_c_result(status);
}

nfl2k5_rhi_result_t nfl2k5_rhi_present(nfl2k5_rhi_t ctx) {
    if (!ctx || !ctx->backend) {
        return NFL2K5_RHI_ERROR_INVALID_PARAM;
    }
    return nfl2k5::rhi::to_c_result(ctx->backend->present());
}

nfl2k5_rhi_result_t nfl2k5_rhi_resize(nfl2k5_rhi_t ctx, uint32_t width, uint32_t height) {
    if (!ctx || !ctx->backend || width == 0 || height == 0) {
        return NFL2K5_RHI_ERROR_INVALID_PARAM;
    }
    ctx->config.width = width;
    ctx->config.height = height;
    return nfl2k5::rhi::to_c_result(ctx->backend->resize(width, height));
}

void nfl2k5_rhi_destroy(nfl2k5_rhi_t ctx) {
    if (!ctx) return;
    if (ctx->backend) {
        ctx->backend->wait_idle();
        ctx->backend.reset();
    }
    delete ctx;
}

} // extern C