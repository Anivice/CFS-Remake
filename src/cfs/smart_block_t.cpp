#include "smart_block_t.h"
#include "utils.h"

void cfs::bitmap_base::init(const uint64_t required_blocks)
{
    *(uint64_t*)&bytes_required_ = cfs::utils::arithmetic::count_cell_with_cell_size(8, required_blocks);
    if (!init_data_array(bytes_required_)) {
        throw cfs::error::bitmap_base_init_data_array_returns_false("Init data array failed");
    }
}

bool cfs::bitmap_base::get_bit(const uint64_t index)
{
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = 0;
    {
        std::lock_guard<std::mutex> lock(array_mtx_);
        c = data_array_[q];
    }

    c >>= r;
    c &= 0x01;
    return c;
}

void cfs::bitmap_base::set_bit(uint64_t index, bool new_bit)
{
    const uint64_t q = index >> 3;
    const uint64_t r = index & 7; // div by 8
    uint8_t c = new_bit & 0x01;
    c <<= r;
    {
        std::lock_guard<std::mutex> lock(array_mtx_);
        if (new_bit) // set to true
        {
            data_array_[q] |= c;
        }
        else // clear bit
        {
            data_array_[q] &= ~c;
        }
    }
}
