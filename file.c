#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_read
 * Description: Reads data from a file, supporting multiple blocks (Bonus).
 */
// 原始：直接去抓 i_block，然後 copy_to_user。
// Bonus: 迴圈邏輯：計算 logical_block_index (目前讀到第幾塊)、查表 i_blocks_array[index]找實體區塊、支援跨區塊連續讀取。
static ssize_t osfs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_read = 0;
    size_t chunk_len;
    uint32_t logical_block_index;
    uint32_t physical_block_no;
    size_t offset_in_block;

    if (*ppos >= osfs_inode->i_size)
        return 0;

    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    while (len > 0) {
        logical_block_index = *ppos / BLOCK_SIZE;
        offset_in_block = *ppos % BLOCK_SIZE;
        chunk_len = BLOCK_SIZE - offset_in_block;
        if (chunk_len > len)
            chunk_len = len;

        // Check if we have a valid block for this logical index
        if (logical_block_index >= osfs_inode->i_blocks) {
            // Reading past allocated blocks (shouldn't happen if i_size is correct)
            break;
        }

        physical_block_no = osfs_inode->i_blocks_array[logical_block_index];
        data_block = sb_info->data_blocks + physical_block_no * BLOCK_SIZE + offset_in_block;

        if (copy_to_user(buf, data_block, chunk_len)) {
            return -EFAULT;
        }

        buf += chunk_len;
        *ppos += chunk_len;
        len -= chunk_len;
        bytes_read += chunk_len;
    }

    return bytes_read;
}


/**
 * Function: osfs_write
 * Description: Writes data to a file, allocating multiple blocks as needed (Bonus).
 */
// 原始：若無 Block 則分配、若寫入超過 4KB 則回傳錯誤或截斷、寫入單一 Block
// Bonus: 迴圈邏輯 (Loop) + 動態分配
//1. 計算目前寫入位置需要第幾個 Block (index)
//2. 如果該 index 還沒分配，呼叫 osfs_alloc_data_block 動態新增
//3. 支援跨區塊連續寫入 (直到 MAX_EXTENTS)
static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{   
    //Step1: Retrieve the inode and filesystem information
    struct inode *inode = file_inode(filp); // VFS inode
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_written = 0;
    int ret;
    struct timespec64 now;

    size_t chunk_len;
    uint32_t logical_block_index;
    uint32_t physical_block_no;
    size_t offset_in_block;

    // Bonus才有迴圈
    // Loop to handle writes that span multiple blocks
    while (len > 0) {
        // 計算目前寫入位置 (*ppos) 對應的是第幾個邏輯區塊 (0, 1, 2, 3, 4...)
        logical_block_index = *ppos / BLOCK_SIZE;
        // 計算在該區塊內的偏移量 (0 ~ 4095)
        offset_in_block = *ppos % BLOCK_SIZE;
        
        // Max file size check
        if (logical_block_index >= MAX_EXTENTS) {
            if (bytes_written > 0) break; // Return what we wrote so far
            return -ENOSPC;
        }

        // Step2: Check if a data block has been allocated; if not, allocate one
        // Allocate new blocks if needed
        // If we need block N, and current i_blocks is N, we need to allocate.
        // Assumes sequential filling.
        if (logical_block_index >= osfs_inode->i_blocks) {
            ret = osfs_alloc_data_block(sb_info, &physical_block_no);
            if (ret) {
                 if (bytes_written > 0) break;
                 return ret;
            }
            // 將申請到的實體區塊號碼存入陣列中 (建立索引)
            osfs_inode->i_blocks_array[logical_block_index] = physical_block_no;
            osfs_inode->i_blocks++;
            inode->i_blocks++; // Update VFS inode blocks count (in 512B units typically, but here simplified)
        } else {
            // 如果已經分配過，直接從陣列查表取得實體區塊號碼
            physical_block_no = osfs_inode->i_blocks_array[logical_block_index];
        }

        // Step 3: Limit the write length to fit within one data block
        // 計算這個區塊還剩下多少空間可以寫
        // 例如：BlockSize是4096，已經寫了4000，那這輪只能再寫96 bytes
        chunk_len = BLOCK_SIZE - offset_in_block; // 多的下輪迴圈再寫
        if (chunk_len > len)
            chunk_len = len;

        // Step 4: Write data from user space to the data block
        // 計算實際記憶體位址：
        // 起始位址 (data_blocks) + 偏移幾個區塊 (physical_block_no * 4096) + 區塊內偏移
        data_block = sb_info->data_blocks + physical_block_no * BLOCK_SIZE + offset_in_block;
        
        // 使用 copy_from_user 將資料從使用者空間 (buf) 複製到核心空間 (data_block)
        if (copy_from_user(data_block, buf, chunk_len)) {
            return -EFAULT;
        }

        buf += chunk_len;
        *ppos += chunk_len;
        len -= chunk_len;
        bytes_written += chunk_len;
    }

    // Step 5: Update inode & osfs_inode attribute
    // 如果寫入後的位置 (*ppos) 超過了原本的檔案大小，就要更新檔案大小 (i_size)
    if (*ppos > osfs_inode->i_size) {
        osfs_inode->i_size = *ppos;
        inode->i_size = *ppos;
    }

    // Update timestamps
    now = current_time(inode);
    inode_set_mtime_to_ts(inode, now);
    inode_set_ctime_to_ts(inode, now);
    
    osfs_inode->__i_mtime = now;
    osfs_inode->__i_ctime = now;
    
    mark_inode_dirty(inode);

    // Step 6: Return the number of bytes written
    return bytes_written;
}

/**
 * Struct: osfs_file_operations
 */
const struct file_operations osfs_file_operations = {
    .open = generic_file_open,
    .read = osfs_read,
    .write = osfs_write,
    .llseek = default_llseek,
};

/**
 * Struct: osfs_file_inode_operations
 */
const struct inode_operations osfs_file_inode_operations = {
    // Add inode operations here
};