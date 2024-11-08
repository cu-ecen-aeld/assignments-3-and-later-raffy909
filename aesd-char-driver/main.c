/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fs.h> // file_operations

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("raffy909"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    
    /**
     * TODO: handle open
     */

	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev;
    filp->f_pos = 0;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

/*          READ & WRITE          */

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    size_t entry_offset_byte = 0;
    size_t bytes_to_copy = 0;
    
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    struct aesd_buffer_entry *entry;
    
    ssize_t retval = 0;
    
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte);
    if (entry == NULL) {
        mutex_unlock(&dev->lock);
        return 0;
    }

    bytes_to_copy = min(count, entry->size - entry_offset_byte);
    PDEBUG("Sending to user %s", entry->buffptr);
    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_copy)) {
        retval = -EFAULT;
        goto exit;
    }

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

exit:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    char *kbuf;
    int newline_pos = -1;
    ssize_t retval = -ENOMEM;
    
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }
    
    /**
     * TODO: handle write
     */
    kbuf = kmalloc(count, GFP_KERNEL);
    if(!kbuf) {
        retval = -ENOMEM;
        goto exit;
    }

    if(copy_from_user(kbuf, buf, count)) {
        retval = -EFAULT;
        goto exit_free;
    }

    int i;
    for(i = 0; i < count; i++) {
        if (kbuf[i] == '\n') {
            newline_pos = i;
            break;
        }
    }

    if(newline_pos == -1) {
        dev->entry.buffptr = krealloc(dev->entry.buffptr, dev->entry.size + count, GFP_KERNEL);
        if (!dev->entry.buffptr) {
            retval = -ENOMEM;
            goto exit_free;
        }
        memcpy((void *)(dev->entry.buffptr + dev->entry.size), kbuf, count);
        dev->entry.size += count;
    } else {
        size_t copy_size = newline_pos + 1;
        
        dev->entry.buffptr = krealloc(dev->entry.buffptr, dev->entry.size  + copy_size, GFP_KERNEL);
        if (!dev->entry.buffptr) {
            retval = -ENOMEM;
            goto exit_free;
        }
        
        memcpy((void *)(dev->entry.buffptr + dev->entry.size ), kbuf, copy_size);
        dev->entry.size  += copy_size;

        aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry);
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }

    retval = count;
exit_free:
    kfree(kbuf);
exit:
    mutex_unlock(&dev->lock);
    return retval;
}

/*          IOCTL & SEEK           */

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int retval;
    struct aesd_seekto seek_cmd;
    struct aesd_dev *dev = filp->private_data;
    size_t total_size = 0;
    uint8_t i;

    PDEBUG("Ioctl cmd: %u arg: %u", cmd, arg);


    if (cmd == AESDCHAR_IOCSEEKTO) {
        PDEBUG("Recived seek-to command");
        if (copy_from_user(&seek_cmd, (const void __user *)arg, sizeof(struct aesd_seekto))) {
            retval = -EINVAL;
        }
        
        PDEBUG("data from userspace: %d, %d", seek_cmd.write_cmd, seek_cmd.write_cmd_offset);

        /*
        if (seek_cmd.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            PDEBUG("Invalid command: %d", seek_cmd.write_cmd);
            return -EINVAL;
        }
        */

        if (mutex_lock_interruptible(&dev->lock)) {
            return -ERESTARTSYS;
        }

        uint32_t target_entry = seek_cmd.write_cmd;
        uint32_t entry_offset = seek_cmd.write_cmd_offset;
        if(dev->buffer.entry[target_entry].buffptr != NULL) { //Checking if the buffer entry exist
            
            size_t total_size = 0;
            struct aesd_buffer_entry *entry;
            AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, i) {
                total_size += entry->size;
            }
            
            if (seek_cmd.write_cmd_offset < total_size){
                PDEBUG("offset found");

                struct aesd_buffer_entry *entry;
                uint8_t index;
                filp->f_pos = 0;

                AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index){
                    if (index != target_entry) {
                        filp->f_pos += entry->size;
                        PDEBUG("1:f_pos:%d",filp->f_pos);
                    } else {
                        filp->f_pos += entry_offset;
                        PDEBUG("2:f_pos:%d", filp->f_pos);
                        retval =  0;
                        goto exit;
                    }
                }
            }
        }
        retval = -1;
    } else {
        PDEBUG("Command is not valid: %u", cmd);
        return -ENOTTY;
    }

exit:
    mutex_unlock(&dev->lock);
    return retval;
}

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    size_t total_size = 0;
    uint8_t i;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("seek called type: %d, offset:%d");

    // Get bufffer current size
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, i) {
        total_size += entry->size;
    }

    switch (whence) {
        case SEEK_SET:
            if (offset < 0 || offset > total_size) {
                return -EINVAL; 
            }
            filp->f_pos = offset;
            break;

        case SEEK_CUR:
            if (filp->f_pos + offset < 0 || filp->f_pos + offset > total_size) {
                return -EINVAL;
            }
            filp->f_pos += offset; 
            break;

        case SEEK_END:
            if (offset > 0 || total_size + offset < 0) {
                return -EINVAL;
            }
            filp->f_pos = total_size + offset;
            break;

        default:
            return -EINVAL;
    }

    if (filp->f_pos < 0 || filp->f_pos > total_size) {
        return -EINVAL;
    }

    return filp->f_pos;
}

/*      DRIVER INIT & CLEANUP      */

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek  =  aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        PDEBUG(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        PDEBUG(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    
    aesd_device.entry.buffptr = NULL;
    aesd_device.entry.size = 0;
    
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    PDEBUG("Freeing temp buffer\n");
    if(aesd_device.entry.buffptr != NULL) {
        kfree(aesd_device.entry.buffptr);
    }

    PDEBUG("Freeing main buffer\n");
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }

    cdev_del(&aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
