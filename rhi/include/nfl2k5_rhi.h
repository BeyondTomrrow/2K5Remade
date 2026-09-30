#ifndef NFL2K5_RHI_H
#define NFL2K5_RHI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Opaque Handle & Return Codes                                              */
/* ========================================================================= */

/**
 * @brief Opaque handle representing an initialized RHI backend instance.
 * The emulator core holds this handle without knowing internal C++ state.
 */
typedef struct nfl2k5_rhi_context* nfl2k5_rhi_t;

typedef enum nfl2k5_rhi_result {
    NFL2K5_RHI_SUCCESS              = 0,
    NFL2K5_RHI_ERROR_INVALID_PARAM  = -1,
    NFL2K5_RHI_ERROR_DEVICE_LOST    = -2,
    NFL2K5_RHI_ERROR_OUT_OF_MEMORY  = -3,
    NFL2K5_RHI_ERROR_UNSUPPORTED    = -4,
    NFL2K5_RHI_ERROR_BACKEND_FAILED = -5
} nfl2k5_rhi_result_t;

/* ========================================================================= */
/* Backend Selection & Configuration                                         */
/* ========================================================================= */

typedef enum nfl2k5_rhi_backend_type {
    NFL2K5_RHI_BACKEND_AUTO   = 0,
    NFL2K5_RHI_BACKEND_VULKAN = 1,
    NFL2K5_RHI_BACKEND_DX12   = 2
} nfl2k5_rhi_backend_type_t;

typedef struct nfl2k5_rhi_init_desc {
    nfl2k5_rhi_backend_type_t backend;              /**< Chosen graphics API backend. */
    void*                     native_window;        /**< OS window handle (e.g. HWND on Windows). */
    uint32_t                  width;                /**< Initial presentation width in pixels. */
    uint32_t                  height;               /**< Initial presentation height in pixels. */
    int                       vsync;                /**< Boolean: 1 enables v-blank sync, 0 unthrottled. */
    int                       validation_layers;    /**< Boolean: 1 enables Vulkan Validation/DX12 Debug Layer. */
} nfl2k5_rhi_init_desc_t;

/* ========================================================================= */
/* Pushbuffer & Frame Management                                             */
/* ========================================================================= */

/**
 * @brief Metadata accompanying a submitted pushbuffer slice.
 */
typedef struct nfl2k5_rhi_pushbuffer_desc {
    const uint32_t* words;          /**< Pointer to raw 32-bit NV2A pushbuffer words. */
    size_t          count;          /**< Count of 32-bit words in this submission. */
    uint32_t        guest_va;       /**< Emulated virtual address where this batch was read from. */
} nfl2k5_rhi_pushbuffer_desc_t;

/* ========================================================================= */
/* Public API Functions                                                      */
/* ========================================================================= */

/**
 * @brief Initializes the selected graphics backend and allocates pipeline resources.
 * 
 * @param desc Pointer to configuration struct.
 * @param out_ctx Pointer to receive the initialized context handle.
 * @return NFL2K5_RHI_SUCCESS on success, or an error code on failure.
 */
nfl2k5_rhi_result_t nfl2k5_rhi_create(const nfl2k5_rhi_init_desc_t* desc, nfl2k5_rhi_t* out_ctx);

/**
 * @brief Submits a chunk of decoded or raw pushbuffer commands to the GPU.
 * 
 * The backend interprets NV2A method offsets, converts vertex microcode or fixed-function
 * state flags into Pipeline State Objects (PSOs), and records draw calls.
 */
nfl2k5_rhi_result_t nfl2k5_rhi_submit_pushbuffer(nfl2k5_rhi_t ctx, const nfl2k5_rhi_pushbuffer_desc_t* desc);

/**
 * @brief Signals a frame boundary and presents the backbuffer to the screen.
 * Resolves frame synchronization barriers and manages swapchain rotation.
 */
nfl2k5_rhi_result_t nfl2k5_rhi_present(nfl2k5_rhi_t ctx);

/**
 * @brief Handles resize events from the host window.
 */
nfl2k5_rhi_result_t nfl2k5_rhi_resize(nfl2k5_rhi_t ctx, uint32_t width, uint32_t height);

/**
 * @brief Flushes all pending hardware queues and releases all allocated graphics resources.
 */
void nfl2k5_rhi_destroy(nfl2k5_rhi_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* NFL2K5_RHI_H */