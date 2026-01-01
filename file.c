#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_read
 * Description: Reads data from a file, supporting multiple blocks (Bonus).
 */
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
static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{   
    struct inode *inode = file_inode(filp);
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

    // Loop to handle writes that span multiple blocks
    while (len > 0) {
        logical_block_index = *ppos / BLOCK_SIZE;
        offset_in_block = *ppos % BLOCK_SIZE;
        
        // Max file size check
        if (logical_block_index >= MAX_EXTENTS) {
            if (bytes_written > 0) break; // Return what we wrote so far
            return -ENOSPC;
        }

        // Allocate new blocks if needed
        // If we need block N, and current i_blocks is N, we need to allocate.
        // Assumes sequential filling.
        if (logical_block_index >= osfs_inode->i_blocks) {
            ret = osfs_alloc_data_block(sb_info, &physical_block_no);
            if (ret) {
                 if (bytes_written > 0) break;
                 return ret;
            }
            osfs_inode->i_blocks_array[logical_block_index] = physical_block_no;
            osfs_inode->i_blocks++;
            inode->i_blocks++; // Update VFS inode blocks count (in 512B units typically, but here simplified)
        } else {
            physical_block_no = osfs_inode->i_blocks_array[logical_block_index];
        }

        chunk_len = BLOCK_SIZE - offset_in_block;
        if (chunk_len > len)
            chunk_len = len;

        data_block = sb_info->data_blocks + physical_block_no * BLOCK_SIZE + offset_in_block;
        
        if (copy_from_user(data_block, buf, chunk_len)) {
            return -EFAULT;
        }

        buf += chunk_len;
        *ppos += chunk_len;
        len -= chunk_len;
        bytes_written += chunk_len;
    }

    // Update inode & osfs_inode attribute
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