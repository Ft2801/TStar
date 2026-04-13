#ifndef STACK_DATA_BLOCK_H
#define STACK_DATA_BLOCK_H

/**
 * @file StackDataBlock.h
 * @brief Memory layout and allocation for per-thread stacking work buffers.
 *
 * StackDataBlock is a monolithic allocation that provides scratch arrays
 * needed during pixel-level stacking operations: pixel stacks, rejection
 * flags, winsorized copies, linear-fit buffers, feathering masks, and
 * drizzle weights.
 *
 * ImageBlock describes a horizontal strip of the output image assigned to
 * a single processing unit (thread).
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>

#include "StackingTypes.h"

namespace Stacking {

// ============================================================================
// ImageBlock
// ============================================================================

/**
 * @brief Describes a horizontal strip of the output image to be processed.
 */
struct ImageBlock {
    int channel;   ///< Color channel (-1 = all channels linked).
    int startRow;  ///< First row of this block (0-indexed).
    int endRow;    ///< Last row (inclusive).
    int height;    ///< Number of rows (endRow - startRow + 1).
};

// ============================================================================
// StackDataBlock
// ============================================================================

/**
 * @brief Per-thread scratch memory for the stacking inner loop.
 *
 * All sub-buffers are carved out of a single malloc so that allocation /
 * deallocation is a single operation and cache locality is improved.
 *
 * Non-copyable, move-only.
 */
struct StackDataBlock {
    // ---- Primary allocation ------------------------------------------------
    void*   tmp;       ///< Single backing allocation for all sub-buffers.
    float** pix;       ///< Per-frame pixel pointers for the current block.
    float** maskPix;   ///< Per-frame feathering mask pointers (may be null).

    // ---- Per-pixel stacks --------------------------------------------------
    float* stack;      ///< Current pixel stack (values from all frames).
    float* o_stack;    ///< Original (pre-sort) copy of the stack.
    float* w_stack;    ///< Winsorized copy (Winsorized/GESDT rejection only).
    int*   rejected;   ///< Rejection flags: 0 = kept, -1 = low, +1 = high.

    // ---- Multi-channel (linked rejection) buffers --------------------------
    float* stackRGB[3];      ///< Per-channel pixel stacks.
    float* w_stackRGB[3];    ///< Per-channel winsorized stacks.
    float* o_stackRGB[3];    ///< Per-channel original stacks.
    int*   rejectedRGB[3];   ///< Per-channel rejection flags.

    // ---- Auxiliary buffers -------------------------------------------------
    float* mstack;   ///< Feathering mask values (if enabled).
    float* dstack;   ///< Drizzle weights (if enabled).

    // ---- Linear fit data ---------------------------------------------------
    float* xf;       ///< x-values for linear fit regression.
    float* yf;       ///< y-values for linear fit regression.
    float  m_x;      ///< Mean of x values.
    float  m_dx2;    ///< Precomputed 1 / sum((x - m_x)^2).

    // ---- Processing state --------------------------------------------------
    int layer;       ///< Current channel being processed.

    // ---- Allocation --------------------------------------------------------

    /**
     * @brief Allocate all scratch buffers in a single contiguous block.
     *
     * @param nbFrames       Number of input frames in the stack.
     * @param pixelsPerBlock  Pixels per block (width * blockHeight).
     * @param numChannels     Number of image channels (1 or 3).
     * @param rejectionType   Rejection algorithm (determines optional buffers).
     * @param hasMask         Whether feathering masks are needed.
     * @param hasDrizzle      Whether drizzle weight buffers are needed.
     * @return true on success, false on allocation failure.
     */
    bool allocate(int nbFrames, size_t pixelsPerBlock, int numChannels,
                  Rejection rejectionType, bool hasMask, bool hasDrizzle)
    {
        const size_t elemSize = sizeof(float);

        // ---- Compute sizes for each sub-buffer ----

        const size_t pixArraySize  = nbFrames * sizeof(float*);
        const size_t blockDataSize = nbFrames * pixelsPerBlock * numChannels * elemSize;
        const size_t maskDataSize  = hasMask ? (nbFrames * pixelsPerBlock * elemSize) : 0;

        const size_t stackSize     = nbFrames * numChannels * elemSize;
        const size_t oStackSize    = nbFrames * numChannels * elemSize;
        const size_t rejectedSize  = nbFrames * numChannels * sizeof(int);

        size_t wStackSize = 0;
        if (rejectionType == Rejection::Winsorized || rejectionType == Rejection::GESDT)
            wStackSize = nbFrames * numChannels * elemSize;

        size_t linearFitSize = 0;
        if (rejectionType == Rejection::LinearFit)
            linearFitSize = 2 * nbFrames * numChannels * sizeof(float);

        const size_t mstackSize = hasMask    ? (nbFrames * elemSize) : 0;
        const size_t dstackSize = hasDrizzle ? (nbFrames * elemSize) : 0;

        const size_t totalSize = blockDataSize + maskDataSize + stackSize
                               + oStackSize + rejectedSize + wStackSize
                               + linearFitSize + mstackSize + dstackSize + 1024;

        // ---- Allocate pointer arrays ----

        pix = static_cast<float**>(std::malloc(pixArraySize));
        if (!pix) return false;

        maskPix = nullptr;
        if (hasMask) {
            maskPix = static_cast<float**>(std::malloc(pixArraySize));
            if (!maskPix) { std::free(pix); return false; }
        }

        // ---- Allocate main contiguous block ----

        tmp = std::malloc(totalSize);
        if (!tmp) {
            std::free(pix);
            if (maskPix) std::free(maskPix);
            return false;
        }

        // ---- Carve sub-buffers from the contiguous block ----

        char* ptr = static_cast<char*>(tmp);

        // 1. Per-frame pixel data.
        for (int f = 0; f < nbFrames; ++f)
            pix[f] = reinterpret_cast<float*>(
                ptr + f * pixelsPerBlock * numChannels * elemSize);
        ptr += blockDataSize;

        // 1b. Per-frame mask data.
        if (hasMask && maskPix) {
            for (int f = 0; f < nbFrames; ++f)
                maskPix[f] = reinterpret_cast<float*>(
                    ptr + f * pixelsPerBlock * elemSize);
            ptr += maskDataSize;
        }

        // 2. Per-channel pixel stacks.
        const size_t singleStackSize = nbFrames * elemSize;

        for (int c = 0; c < 3; ++c) {
            stackRGB[c]    = nullptr;
            o_stackRGB[c]  = nullptr;
            rejectedRGB[c] = nullptr;
            w_stackRGB[c]  = nullptr;
        }

        for (int c = 0; c < numChannels && c < 3; ++c) {
            stackRGB[c] = reinterpret_cast<float*>(ptr);
            ptr += singleStackSize;
        }
        stack = stackRGB[0];

        // 3. Per-channel original (pre-sort) stacks.
        for (int c = 0; c < numChannels && c < 3; ++c) {
            o_stackRGB[c] = reinterpret_cast<float*>(ptr);
            ptr += singleStackSize;
        }
        o_stack = o_stackRGB[0];

        // Align pointer for int access.
        size_t alignment = reinterpret_cast<size_t>(ptr) % sizeof(int);
        if (alignment > 0)
            ptr += sizeof(int) - alignment;

        // 4. Per-channel rejection flags.
        const size_t singleRejSize = nbFrames * sizeof(int);
        for (int c = 0; c < numChannels && c < 3; ++c) {
            rejectedRGB[c] = reinterpret_cast<int*>(ptr);
            ptr += singleRejSize;
        }
        rejected = rejectedRGB[0];

        // 5. Per-channel winsorized stacks (optional).
        if (wStackSize > 0) {
            for (int c = 0; c < numChannels && c < 3; ++c) {
                w_stackRGB[c] = reinterpret_cast<float*>(ptr);
                ptr += singleStackSize;
            }
            w_stack = w_stackRGB[0];
        } else {
            w_stack = nullptr;
        }

        // 6. Linear fit buffers (optional).
        if (linearFitSize > 0) {
            xf = reinterpret_cast<float*>(ptr);
            yf = xf + nbFrames;
            ptr += linearFitSize;

            m_x   = (nbFrames - 1) * 0.5f;
            m_dx2 = 0.0f;
            for (int j = 0; j < nbFrames; ++j) {
                float dx = j - m_x;
                xf[j]  = 1.0f / (j + 1);
                m_dx2 += (dx * dx - m_dx2) * xf[j];
            }
            m_dx2 = 1.0f / m_dx2;
        } else {
            xf    = nullptr;
            yf    = nullptr;
            m_x   = 0.0f;
            m_dx2 = 0.0f;
        }

        // 7. Feathering mask stack (optional).
        if (mstackSize > 0) {
            mstack = reinterpret_cast<float*>(ptr);
            ptr += mstackSize;
        } else {
            mstack = nullptr;
        }

        // 8. Drizzle weight stack (optional).
        if (dstackSize > 0) {
            dstack = reinterpret_cast<float*>(ptr);
            ptr += dstackSize;
        } else {
            dstack = nullptr;
        }

        layer = 0;
        return true;
    }

    // ---- Deallocation ------------------------------------------------------

    /** @brief Free all allocated memory and reset pointers. */
    void deallocate()
    {
        if (pix)     { std::free(pix);     pix     = nullptr; }
        if (maskPix) { std::free(maskPix); maskPix = nullptr; }
        if (tmp)     { std::free(tmp);     tmp     = nullptr; }

        stack    = nullptr;
        o_stack  = nullptr;
        w_stack  = nullptr;
        rejected = nullptr;
        xf       = nullptr;
        yf       = nullptr;
        mstack   = nullptr;
        dstack   = nullptr;
    }

    // ---- Constructors / Destructor -----------------------------------------

    StackDataBlock()
        : tmp(nullptr), pix(nullptr), maskPix(nullptr),
          stack(nullptr), o_stack(nullptr), w_stack(nullptr), rejected(nullptr),
          mstack(nullptr), dstack(nullptr),
          xf(nullptr), yf(nullptr), m_x(0), m_dx2(0), layer(0)
    {}

    ~StackDataBlock() { deallocate(); }

    // Non-copyable.
    StackDataBlock(const StackDataBlock&)            = delete;
    StackDataBlock& operator=(const StackDataBlock&) = delete;

    // Movable.
    StackDataBlock(StackDataBlock&& other) noexcept
    {
        tmp      = other.tmp;
        pix      = other.pix;
        maskPix  = other.maskPix;
        stack    = other.stack;
        o_stack  = other.o_stack;
        w_stack  = other.w_stack;
        rejected = other.rejected;
        mstack   = other.mstack;
        dstack   = other.dstack;
        xf       = other.xf;
        yf       = other.yf;
        m_x      = other.m_x;
        m_dx2    = other.m_dx2;
        layer    = other.layer;

        for (int c = 0; c < 3; ++c) {
            stackRGB[c]    = other.stackRGB[c];
            o_stackRGB[c]  = other.o_stackRGB[c];
            w_stackRGB[c]  = other.w_stackRGB[c];
            rejectedRGB[c] = other.rejectedRGB[c];
        }

        // Null out the source to prevent double-free.
        other.tmp      = nullptr;
        other.pix      = nullptr;
        other.maskPix  = nullptr;
        other.stack    = nullptr;
        other.o_stack  = nullptr;
        other.w_stack  = nullptr;
        other.rejected = nullptr;
        other.mstack   = nullptr;
        other.dstack   = nullptr;
        other.xf       = nullptr;
        other.yf       = nullptr;
    }
};

// ============================================================================
// Block Partitioning
// ============================================================================

/**
 * @brief Partition the output image into horizontal blocks for parallel processing.
 *
 * Divides the image height into blocks sized to fit within the specified
 * memory budget.  All channels are processed together per block (linked
 * rejection).
 *
 * @param[out] blocks             Computed block descriptions.
 * @param[in]  maxRowsInMemory    Maximum rows that fit in the memory budget.
 * @param[in]  width              Image width (unused; kept for API compat).
 * @param[in]  height             Image height.
 * @param[in]  channels           Number of channels (unused; linked mode).
 * @param[in]  nbThreads          Number of parallel threads.
 * @param[out] largestBlockHeight Height of the tallest block.
 * @return 0 on success, -1 on invalid parameters.
 */
inline int computeParallelBlocks(
    std::vector<ImageBlock>& blocks,
    int64_t maxRowsInMemory,
    int width, int height, int channels,
    int nbThreads,
    int& largestBlockHeight)
{
    (void)width;
    (void)channels;

    if (nbThreads < 1 || maxRowsInMemory < 1)
        return -1;

    const int64_t totalRows = height;

    // Determine the minimum number of blocks so that each block's row count
    // does not exceed maxRowsInMemory when divided among threads.
    int candidate = nbThreads;
    while ((maxRowsInMemory * candidate) / nbThreads < totalRows)
        candidate++;

    const int nbBlocks       = candidate;
    const int heightOfBlocks = height / nbBlocks;
    int       remainder      = height % nbBlocks;

    blocks.clear();
    blocks.reserve(nbBlocks);
    largestBlockHeight = 0;

    int row = 0;
    for (int j = 0; j < nbBlocks && row < height; ++j) {
        ImageBlock block;
        block.channel  = -1;  // -1 = all channels linked.
        block.startRow = row;

        int h = heightOfBlocks;
        if (remainder > 0) { h++; remainder--; }

        block.height = h;
        block.endRow = row + h - 1;
        if (block.endRow >= height)
            block.endRow = height - 1;
        block.height = block.endRow - block.startRow + 1;

        if (block.height > largestBlockHeight)
            largestBlockHeight = block.height;

        blocks.push_back(block);
        row += h;
    }

    return 0;
}

} // namespace Stacking

#endif // STACK_DATA_BLOCK_H