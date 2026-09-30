#pragma once

#include "rhi_common.hpp"
#include <span>

namespace nfl2k5::rhi {

    /**
     * @brief Abstract base class defining internal backend operations.
     */
    class IRhiBackend {
    public:
        virtual ~IRhiBackend() = default;

        /**
         * @brief Performs low-level device initialization, queue creation, and swapchain setup.
         */
        [[nodiscard]] virtual Status initialize(const nfl2k5_rhi_init_desc_t& desc) = 0;

        /**
         * @brief Parses and executes a block of raw NV2A pushbuffer words.
         * @param words A contiguous span of 32-bit pushbuffer words.
         * @param guest_va The base virtual address in the emulated guest address space.
         */
        [[nodiscard]] virtual Status submit_pushbuffer(std::span<const uint32_t> words, uint32_t guest_va) = 0;

        /**
         * @brief Finalizes the current command list and presents the swapchain buffer.
         */
        [[nodiscard]] virtual Status present() = 0;

        /**
         * @brief Resizes swapchain render targets when the window dimensions change.
         */
        [[nodiscard]] virtual Status resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief Waits for all pending GPU queues to finish work before destruction.
         */
        virtual void wait_idle() = 0;
    };

    /**
     * @brief Factory declarations for backend instantiation.
     */
    std::unique_ptr<IRhiBackend> create_dx12_backend();
    std::unique_ptr<IRhiBackend> create_vulkan_backend();

} // namespace nfl2k5::rhi