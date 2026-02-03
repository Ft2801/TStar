
#ifndef STACK_DATA_BLOCK_H
#define STACK_DATA_BLOCK_H

#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
#include "StackingTypes.h"

namespace Stacking {


struct ImageBlock {
    int channel;      // Which color channel (0, 1, 2 for RGB)
    int startRow;     // First row of this block (0-indexed)
    int endRow;       // Last row of this block (inclusive)
    int height;       // Number of rows in this block (endRow - startRow + 1)
};

struct StackDataBlock {
    // The single memory allocation that holds all other buffers
    void* tmp;
    
    // Pointers to pixel data for each image in the current block
    // pix[frame] points to frame's pixel data for this block
    float** pix;
    float** maskPix; // Holds mask values (0..1) for feathering

    // Current pixel stack (values from all frames for one pixel position)
    float* stack;

    // Original unsorted stack (needed for weighted mean after sorting)
    float* o_stack;
    
    // Winsorized stack (used by winsorized sigma clipping)
    float* w_stack;
    
    // Rejection flags: 0 = kept, -1 = low rejected, +1 = high rejected
    int* rejected;
    
    // LINKED REJECTION SUPPORT
    // We need separate buffers for each channel to process them simultaneously
    float* stackRGB[3];
    float* w_stackRGB[3];
    float* o_stackRGB[3];
    int* rejectedRGB[3];
    
    // Mask values for feathering (if enabled)
    float* mstack;
    
    // Drizzle weights (if enabled)
    float* dstack;
    
    // Linear fit data (for LINEAR_FIT rejection)
    float* xf;
    float* yf;
    float m_x;      // Mean of x values
    float m_dx2;    // Precomputed 1/sum((x-m_x)^2)
    
    // Current channel being processed
    int layer;
    
    bool allocate(int nbFrames, size_t pixelsPerBlock, int numChannels,
                  Rejection rejectionType, bool hasMask, bool hasDrizzle) {
        // Calculate total buffer size needed
        size_t elemSize = sizeof(float);
        
        // pix[nbFrames] array of pointers 
        size_t pixArraySize = nbFrames * sizeof(float*);
        
        // Block data: nbFrames * pixelsPerBlock * numChannels floats
        size_t blockDataSize = nbFrames * pixelsPerBlock * numChannels * elemSize;
        
        // Mask data (if enabled): nbFrames * pixelsPerBlock floats (single channel mask)
        size_t maskDataSize = hasMask ? (nbFrames * pixelsPerBlock * elemSize) : 0;

        // Stacks (per pixel): nbFrames * numChannels floats
        size_t stackSize = nbFrames * numChannels * elemSize;
        size_t oStackSize = nbFrames * numChannels * elemSize;
        size_t rejectedSize = nbFrames * numChannels * sizeof(int);
        
        // w_stack (for Winsorized / GESDT)
        size_t wStackSize = 0;
        if (rejectionType == Rejection::Winsorized || rejectionType == Rejection::GESDT) {
            wStackSize = nbFrames * numChannels * elemSize;
        }
        
        // Linear fit buffers (per-channel allocation confirmed)
        // We allocate 2 * nbFrames * numChannels, so this fully supports multi-channel rejection.
        size_t linearFitSize = 0;
        if (rejectionType == Rejection::LinearFit) {
            linearFitSize = 2 * nbFrames * numChannels * sizeof(float); 
        }
        
        // Mask/Drizzle stacks (usually single channel output driven?)
        // Keep as is (nbFrames size) for now.
        size_t mstackSize = hasMask ? (nbFrames * elemSize) : 0;
        size_t dstackSize = hasDrizzle ? (nbFrames * elemSize) : 0;
        
        // Total size with alignment padding
        size_t totalSize = blockDataSize + maskDataSize + stackSize + oStackSize + rejectedSize + 
                          wStackSize + linearFitSize + mstackSize + dstackSize + 1024;
        
        // Allocate pix array
        pix = static_cast<float**>(std::malloc(pixArraySize));
        if (!pix) return false;
        
        maskPix = nullptr;
        if (hasMask) {
            maskPix = static_cast<float**>(std::malloc(pixArraySize));
            if (!maskPix) { std::free(pix); return false; }
        }
        
        // Allocate main memory block
        tmp = std::malloc(totalSize);
        if (!tmp) {
            std::free(pix);
            if(maskPix) std::free(maskPix);
            return false;
        }
        
        // Setup pointers
        char* ptr = static_cast<char*>(tmp);
        
        // 1. Block Pixels (Interleaved blocks)
        for (int f = 0; f < nbFrames; ++f) {
            pix[f] = reinterpret_cast<float*>(ptr + f * pixelsPerBlock * numChannels * elemSize);
        }
        ptr += blockDataSize;
        
        // 1b. Mask Pixels
        if (hasMask && maskPix) {
             for (int f = 0; f < nbFrames; ++f) {
                 maskPix[f] = reinterpret_cast<float*>(ptr + f * pixelsPerBlock * elemSize);
             }
             ptr += maskDataSize;
        }
        
        // 2. Stacks (stackRGB)
        // Divide the big chunk into numChannels small chunks
        size_t singleStackSize = nbFrames * elemSize;
        
        for(int c=0; c<3; ++c) {
             stackRGB[c] = nullptr; 
             o_stackRGB[c] = nullptr;
             rejectedRGB[c] = nullptr;
             w_stackRGB[c] = nullptr;
        }

        for (int c = 0; c < numChannels && c < 3; ++c) {
            stackRGB[c] = reinterpret_cast<float*>(ptr);
            ptr += singleStackSize;
        }
        stack = stackRGB[0]; // Legacy pointer
        if (numChannels < 3) ptr += (3 - numChannels) * 0; // No padding needed really
        
        // 3. Original Stacks
        for (int c = 0; c < numChannels && c < 3; ++c) {
            o_stackRGB[c] = reinterpret_cast<float*>(ptr);
            ptr += singleStackSize;
        }
        o_stack = o_stackRGB[0];

        // Align for int 
        size_t alignment = reinterpret_cast<size_t>(ptr) % sizeof(int);
        if (alignment > 0) ptr += sizeof(int) - alignment;

        // 4. Rejected Flags
        size_t singleRejSize = nbFrames * sizeof(int);
        for (int c = 0; c < numChannels && c < 3; ++c) {
            rejectedRGB[c] = reinterpret_cast<int*>(ptr);
            ptr += singleRejSize;
        }
        rejected = rejectedRGB[0];

        // 5. Winsorized Stacks
        if (wStackSize > 0) {
            for (int c = 0; c < numChannels && c < 3; ++c) {
                w_stackRGB[c] = reinterpret_cast<float*>(ptr);
                ptr += singleStackSize;
            }
            w_stack = w_stackRGB[0];
        } else {
             w_stack = nullptr;
        }
        
        // Linear fit (if needed)
        if (linearFitSize > 0) {
            xf = reinterpret_cast<float*>(ptr);
            yf = xf + nbFrames;
            ptr += linearFitSize;
            
            m_x = (nbFrames - 1) * 0.5f;
            m_dx2 = 0.0f;
            for (int j = 0; j < nbFrames; ++j) {
                float dx = j - m_x;
                xf[j] = 1.0f / (j + 1);
                m_dx2 += (dx * dx - m_dx2) * xf[j];
            }
            m_dx2 = 1.0f / m_dx2;
        } else {
            xf = nullptr;
            yf = nullptr;
            m_x = 0.0f;
            m_dx2 = 0.0f;
        }
        
        // mstack (if masking)
        if (mstackSize > 0) {
            mstack = reinterpret_cast<float*>(ptr);
            ptr += mstackSize;
        } else {
            mstack = nullptr;
        }
        
        // dstack (if drizzle)
        if (dstackSize > 0) {
            dstack = reinterpret_cast<float*>(ptr);
            ptr += dstackSize;
        } else {
            dstack = nullptr;
        }
        
        layer = 0;
        return true;
    }
    
    /**
     * Free all allocated memory
     */
    void deallocate() {
        if (pix) {
            std::free(pix);
            pix = nullptr;
        }
        if (maskPix) {
             std::free(maskPix);
             maskPix = nullptr;
        }
        if (tmp) {
            std::free(tmp);
            tmp = nullptr;
        }
        stack = nullptr;
        o_stack = nullptr;
        w_stack = nullptr;
        rejected = nullptr;
        xf = nullptr;
        yf = nullptr;
        mstack = nullptr;
        dstack = nullptr;
    }
    
    StackDataBlock() : tmp(nullptr), pix(nullptr), stack(nullptr), o_stack(nullptr),
                       w_stack(nullptr), rejected(nullptr), mstack(nullptr), dstack(nullptr),
                       xf(nullptr), yf(nullptr), m_x(0), m_dx2(0), layer(0) {}
    
    ~StackDataBlock() {
        deallocate();
    }
    
    // Non-copyable
    StackDataBlock(const StackDataBlock&) = delete;
    StackDataBlock& operator=(const StackDataBlock&) = delete;
    
    // Movable
    StackDataBlock(StackDataBlock&& other) noexcept {
        tmp = other.tmp;
        pix = other.pix;
        stack = other.stack;
        o_stack = other.o_stack;
        w_stack = other.w_stack;
        rejected = other.rejected;
        mstack = other.mstack;
        dstack = other.dstack;
        xf = other.xf;
        yf = other.yf;
        m_x = other.m_x;
        m_dx2 = other.m_dx2;
        layer = other.layer;
        
        other.tmp = nullptr;
        other.pix = nullptr;
        other.stack = nullptr;
        other.o_stack = nullptr;
        other.w_stack = nullptr;
        other.rejected = nullptr;
        other.mstack = nullptr;
        other.dstack = nullptr;
        other.xf = nullptr;
        other.yf = nullptr;
    }
};

inline int computeParallelBlocks(
    std::vector<ImageBlock>& blocks,
    int64_t maxRowsInMemory,
    int width, int height, int channels,
    int nbThreads,
    int& largestBlockHeight
) {
    (void)width; // Unused parameter
    (void)channels; // Unused (we process all channels at once now)
    
    if (nbThreads < 1 || maxRowsInMemory < 1) return -1;
    
    int64_t totalRows = height; // Total rows to process (all channels at once)
    
    // Calculate minimum number of blocks needed based on memory
    // maxRowsInMemory passed here should be "Max Rows of Multi-Channel Data"
    int candidate = nbThreads;
    while ((maxRowsInMemory * candidate) / nbThreads < totalRows) {
        candidate++;
    }
    
    // Simplify: Just split height by threads/blocks
    int nbBlocks = candidate;
    int heightOfBlocks = height / nbBlocks;
    int remainder = height % nbBlocks;
    
    blocks.clear();
    blocks.reserve(nbBlocks);
    largestBlockHeight = 0;
    
    int row = 0;
    for (int j = 0; j < nbBlocks && row < height; ++j) {
        ImageBlock block;
        block.channel = -1; // -1 means "All Channels Linked"
        block.startRow = row;
        
        int h = heightOfBlocks;
        if (remainder > 0) {
            h++;
            remainder--;
        }
        
        block.height = h;
        block.endRow = row + h - 1;
        if (block.endRow >= height) block.endRow = height - 1;
        block.height = block.endRow - block.startRow + 1;
        
        if (block.height > largestBlockHeight) {
            largestBlockHeight = block.height;
        }
        
        blocks.push_back(block);
        row += h;
    }
    
    return 0;
}

} // namespace Stacking

#endif // STACK_DATA_BLOCK_H
