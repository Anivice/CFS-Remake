#ifndef CFS_CFSBASICCOMPONENTS_H
#define CFS_CFSBASICCOMPONENTS_H

#include "cfs.h"
#include "smart_block_t.h"

namespace cfs
{
    class cfs_journaling_t
    {
        cfs::filesystem * parent_fs_governor_;
        const uint64_t journal_start_;
        const uint64_t journal_end_;
        const uint64_t block_size_;
        const uint64_t capacity_ = 0;

        char * journal_raw_buffer_;
        char * journal_body_;

        journal_header_t * journal_header_; // start of the ring
        journal_header_t * journal_header_cow_; // end of the ring
        std::mutex mutex_;

        /// put a 8bit data into the journal
        /// @param c 8bit data
        void putc(const char c)
        {
            journal_body_[journal_header_->head] = c;
            journal_header_->head = (journal_header_->head + 1) % capacity_;
            if (journal_header_->size < capacity_) {
                ++journal_header_->size;
            } else {
                // buffer full - discard the oldest element
                journal_header_->tail = (journal_header_->tail + 1) % capacity_;
            }
        }

        /// dump all journal data
        [[nodiscard]] std::vector<uint8_t> dump() const
        {
            std::vector<uint8_t> out;
            out.reserve(journal_header_->size);
            for (size_t i = 0; i < journal_header_->size; ++i) {
                const size_t index = (journal_header_->tail + i) % capacity_;
                out.push_back(journal_body_[index]);
            }
            return out;
        }

    public:
        explicit cfs_journaling_t(cfs::filesystem * parent_fs_governor) :
            parent_fs_governor_(parent_fs_governor),
            journal_start_(parent_fs_governor->static_info_.journal_start),
            journal_end_(parent_fs_governor->static_info_.journal_end),
            block_size_(parent_fs_governor->static_info_.block_size)
        {
            for (auto i = parent_fs_governor->static_info_.journal_start; i < parent_fs_governor->static_info_.journal_end; i++) {
                parent_fs_governor->bitlocker_.lock(i);
            }

            journal_raw_buffer_ = parent_fs_governor->file_.data() + journal_start_ * block_size_;
            journal_body_ = journal_raw_buffer_ + sizeof(journal_header_t);
            journal_header_ = (journal_header_t*)journal_raw_buffer_;
            journal_header_cow_ = (journal_header_t*)(parent_fs_governor->file_.data() + journal_end_ * block_size_ - sizeof(journal_header_t));
            *(uint64_t*)&capacity_ = (journal_end_ - journal_start_) * block_size_ - (sizeof(journal_header_t) * 2);
        }

        ~cfs_journaling_t()
        {
            for (auto i = parent_fs_governor_->static_info_.journal_start; i < parent_fs_governor_->static_info_.journal_end; i++)
            {
                parent_fs_governor_->bitlocker_.unlock(i);
            }
        }

        /// dump all journal actions
        [[nodiscard]] std::vector < cfs_action_t > dump_actions()
        {
            std::lock_guard<std::mutex> guard(mutex_);
            std::vector < cfs_action_t > actions;
            auto data = dump();
            for (uint64_t i = 0; i < data.size();)
            {
                if (const auto * action = reinterpret_cast<cfs_action_t *>(data.data() + i);
                    action->cfs_magic == cfs_magick_number)
                {
                    actions.push_back(*action);
                    i += sizeof(cfs_action_t);
                } else {
                    i++;
                }
            }

            return actions;
        }

        void push_action(
            const uint64_t action,
            const uint64_t action_param0 = 0,
            const uint64_t action_param1 = 0,
            const uint64_t action_param2 = 0,
            const uint64_t action_param3 = 0,
            const uint64_t action_param4 = 0)
        {
            std::lock_guard<std::mutex> guard(mutex_);
            cfs_action_t j_action = { };
            j_action.cfs_magic = cfs_magick_number;
            j_action.action_data.action_plain = {
                .action = action,
                .action_param0 = action_param0,
                .action_param1 = action_param1,
                .action_param2 = action_param2,
                .action_param3 = action_param3,
                .action_param4 = action_param4,
            };
            j_action.action_param_crc64 = cfs::utils::arithmetic::hashcrc64((uint8_t*)&j_action.action_data, sizeof(j_action.action_data));
            *journal_header_cow_ = *journal_header_;
            for (int i = 0; i < sizeof(j_action); i++) {
                putc(reinterpret_cast<const char *>(&j_action)[i]);
            }
        }

        NO_COPY_OBJ(cfs_journaling_t);
    };
    class cfs_bitmap_singular_t : public bitmap_base
    {
    public:
        explicit cfs_bitmap_singular_t(char * mapped_area, uint64_t data_block_numbers);

        /// get CRC64 of the whole bitmap
        [[nodiscard]] uint64_t dump_crc64() const;
    };

    class cfs_bitmap_block_mirroring_t
    {
        cfs_bitmap_singular_t mirror1;
        cfs_bitmap_singular_t mirror2;
        cfs::filesystem * parent_fs_governor_;

    public:
        explicit cfs_bitmap_block_mirroring_t(cfs::filesystem * parent_fs_governor);

        /// Get bit at the specific location
        /// @param index Bit Index
        /// @return The bit at the specific location
        /// @throws cfs::error::assertion_failed Out of bounds
        bool get_bit(uint64_t index);

        /// Set the bit at the specific location
        /// @param index Bit Index
        /// @param new_bit The new bit value
        /// @return NONE
        /// @throws cfs::error::assertion_failed Out of bounds
        void set_bit(uint64_t index, bool new_bit);
    };
}

#endif //CFS_CFSBASICCOMPONENTS_H