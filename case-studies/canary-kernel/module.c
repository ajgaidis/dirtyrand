/*
 * Simplified Kernel Stack Canary Trigger Module for Linux 6.12.11
 * 
 * This kernel module intentionally triggers stack canary violations
 * by performing a large buffer overflow guaranteed to hit the canary.
 * 
 * WARNING: This module WILL crash your kernel!
 * Only use in a test environment.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/umh.h>

#define MODULE_NAME "canary_trigger"
#define PROC_ENTRY_NAME "canary_test"
#define BUFFER_SIZE 64

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anonymous Submission #60");
MODULE_DESCRIPTION("Module vulnerable to kernel stack buffer overflow");
MODULE_VERSION("1.0");

static struct proc_dir_entry *proc_entry;


/*
 * Function to extract canary without causing kernel panic
 * This reads the canary value and returns it in the buffer
 */
static noinline unsigned long __attribute__((optimize("O0"))) extract_canary_safely(void)
{
    char local_buffer[BUFFER_SIZE] = {0};  /* This will have a stack canary */
    unsigned long *canary_ptr;
    unsigned long canary_value;
    
    /* Initialize buffer to avoid compiler warnings */
    memset(local_buffer, 'X', BUFFER_SIZE);
    
    /* Calculate canary location */
    canary_ptr = (unsigned long *)(local_buffer + BUFFER_SIZE);
    canary_value = *canary_ptr;
    
    printk(KERN_INFO "[%s] === SAFE CANARY EXTRACTION ===\n", MODULE_NAME);
    printk(KERN_INFO "[%s] Buffer: %p, Canary ptr addr: %p\n", MODULE_NAME, local_buffer, canary_ptr);
    printk(KERN_INFO "[%s] Extracted canary: 0x%016lx\n", MODULE_NAME, canary_value);
    
    return canary_value;
}



/*
 * Vulnerable function that copies user data to the local kernel buffer with no length check
 */
static noinline void __attribute__((optimize("O0"))) vuln_func(const char *user_data, size_t data_len)
{
    char local_buffer[BUFFER_SIZE] = {0};  /* This will have a stack canary */
    unsigned long canary_value;
    unsigned long *stack_ptr;
    
    /* Initialize buffer to avoid compiler warnings */
    memset(local_buffer, 'Y', BUFFER_SIZE);
    
    printk(KERN_INFO "[%s] === CANARY READING DEMONSTRATION ===\n", MODULE_NAME);
    printk(KERN_INFO "[%s] Local buffer at: %p (size: %d bytes)\n", MODULE_NAME, local_buffer, BUFFER_SIZE);
    printk(KERN_INFO "[%s] User data length: %zu bytes\n", MODULE_NAME, data_len);
    
    /* Calculate where the canary should be located */
    /* On x86_64, canary is typically at buffer + buffer_size + alignment */
    stack_ptr = (unsigned long *)(local_buffer + BUFFER_SIZE);
    
    /* Read the canary value before any overflow */
    canary_value = *stack_ptr;
    printk(KERN_INFO "[%s] Original canary value: 0x%016lx\n", MODULE_NAME, canary_value);
    printk(KERN_INFO "[%s] Canary location: %p\n", MODULE_NAME, stack_ptr);
    
    /* Show stack layout */
    printk(KERN_INFO "[%s] Stack layout:\n", MODULE_NAME);
    printk(KERN_INFO "[%s]   Buffer:  %p - %p\n", MODULE_NAME, local_buffer, local_buffer + BUFFER_SIZE - 1);
    printk(KERN_INFO "[%s]   Canary:  %p (value: 0x%016lx)\n", MODULE_NAME, stack_ptr, *stack_ptr);
    printk(KERN_INFO "[%s]   Next:    %p (value: 0x%016lx)\n", MODULE_NAME, stack_ptr + 1, *(stack_ptr + 1));
    
    if (data_len <= BUFFER_SIZE) {
        /* Safe copy - doesn't touch canary */
        memcpy(local_buffer, user_data, data_len);
        printk(KERN_INFO "[%s] Safe copy complete, canary unchanged: 0x%016lx\n", MODULE_NAME, *stack_ptr);
    } else {
        /* Controlled overflow - overflow exactly as much as input provides */
        printk(KERN_WARNING "[%s] CONTROLLED OVERFLOW: Overflowing with input data\n", MODULE_NAME);
        
        size_t overflow_bytes = data_len - BUFFER_SIZE;
        printk(KERN_INFO "[%s] Overflow size: %zu bytes beyond buffer\n", MODULE_NAME, overflow_bytes);
        
        /* Single memcpy - overflow exactly based on input size */
        memcpy(local_buffer, user_data, data_len);
        
        printk(KERN_INFO "[%s] Overflow complete, canary now: 0x%016lx\n", MODULE_NAME, *stack_ptr);
    }
    
    printk(KERN_INFO "[%s] Final canary value: 0x%016lx\n", MODULE_NAME, *stack_ptr);
    printk(KERN_INFO "[%s] Returning from function (canary check happens here)\n", MODULE_NAME);
    
}
/**
 * dummy function that calls the kernel vulnerable function after padding with 3 64 bit variables
 * These variables will later be overwritten by the rop chain
 */
static noinline unsigned long __attribute__((optimize("O0"))) do_vuln_func(const char *kernel_buffer, size_t count){
    //char dummy_padding[512] __attribute__((unused)) = {0}; // Larger padding to push canary away from overflow
    unsigned long dummy_long1 __attribute__((unused)) = 0xAAAAAAAAAAAAAAAUL;
    unsigned long dummy_long2 __attribute__((unused)) = 0xBBBBBBBBBBBBBBBBUL;
    unsigned long dummy_long3 __attribute__((unused)) = 0xCCCCCCCCCCCCCCCCUL;

    vuln_func(kernel_buffer, count);
    return count;
}


/*
 * Proc file write handler
 * Supports different modes based on input:
 * - "extract" - safely extract canary value
 * - other data - trigger overflow
 */
static ssize_t canary_test_write(struct file *file, const char __user *buffer, 
                                size_t count, loff_t *pos)
{
    char *kernel_buffer;
    unsigned long canary;
    
    printk(KERN_INFO "[%s] Received %zu bytes from user\n", MODULE_NAME, count);
    
    /* Allocate kernel buffer for user data */
    kernel_buffer = kmalloc(count + 1, GFP_KERNEL);
    if (!kernel_buffer) {
        printk(KERN_ERR "[%s] could not allocate kernel buffer\n", MODULE_NAME);
        return 0;
    }
    
    /* Copy data from userspace */
    if (copy_from_user(kernel_buffer, buffer, count)) {
        printk(KERN_ERR "[%s] Failed to copy from user\n", MODULE_NAME);
        kfree(kernel_buffer);
        return 0;
    }
    kernel_buffer[count] = '\0';  /* Null terminate for string operations */
    
    /* Check for special commands */
    if (strncmp(kernel_buffer, "extract", 7) == 0) {
        printk(KERN_INFO "[%s] === SAFE CANARY EXTRACTION MODE ===\n", MODULE_NAME);
        canary = extract_canary_safely();
        printk(KERN_INFO "[%s] Canary extracted: 0x%016lx\n", MODULE_NAME, canary);
        
    } else {
        printk(KERN_WARNING "[%s] === OVERFLOW MODE WITH CANARY READING ===\n", MODULE_NAME);
        
        /* Trigger the stack canary reading/overflow with user data */
        do_vuln_func(kernel_buffer,count);
    }
    
    /* Clean up */
    kfree(kernel_buffer);
    
    /* If we reach here, canary might not have been triggered */
    printk(KERN_INFO "[%s] Operation completed\n", MODULE_NAME);
    
    return count;
}

/*
 * Proc file read handler
 * Provides usage information
 */
static ssize_t canary_test_read(struct file *file, char __user *buffer, 
                               size_t count, loff_t *pos)
{
    static const char usage[] = 
        "Kernel Stack Canary Reader and Trigger\n"
        "======================================\n"
        "\n"
        "Buffer size: 64 bytes\n"
        "\n"
        "Usage modes:\n"
        "  echo 'extract' > /proc/canary_test      # Safely extract canary value\n"
        "  echo 'short_data' > /proc/canary_test   # Safe (< 64 bytes)\n"
        "  python3 -c \"print('A' * 100)\" > /proc/canary_test  # Overflow with canary reading\n"
        "\n"
        "The module will:\n"
        "  - Show original canary value\n"
        "  - Display stack layout\n"
        "  - Monitor canary corruption during overflow\n"
        "  - Show byte-by-byte canary changes\n"
        "\n"
        "Check dmesg for detailed output.\n"
        "WARNING: Large overflows WILL crash the kernel!\n"
        "\n";
    
    if (*pos >= sizeof(usage) - 1)
        return 0;
    
    if (count > sizeof(usage) - 1 - *pos)
        count = sizeof(usage) - 1 - *pos;
    
    if (copy_to_user(buffer, usage + *pos, count))
        return -EFAULT;
    
    *pos += count;
    return count;
}

static const struct proc_ops canary_test_ops = {
    .proc_read = canary_test_read,
    .proc_write = canary_test_write,
};

/*
 * Module initialization
 */
static int __init canary_trigger_init(void)
{
    printk(KERN_INFO "[%s] Loading User-Controlled Stack Canary Trigger\n", MODULE_NAME);
    
    /* Create proc entry */
    proc_entry = proc_create(PROC_ENTRY_NAME, 0666, NULL, &canary_test_ops);
    if (!proc_entry) {
        printk(KERN_ERR "[%s] Failed to create /proc/%s\n", MODULE_NAME, PROC_ENTRY_NAME);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "[%s] Created /proc/%s (buffer size: %d bytes)\n", MODULE_NAME, PROC_ENTRY_NAME, BUFFER_SIZE);
    printk(KERN_WARNING "[%s] *** WARNING: Overflow will crash the kernel! ***\n", MODULE_NAME);
    
    return 0;
}

/*
 * Module cleanup
 */
static void __exit canary_trigger_exit(void)
{
    if (proc_entry) {
        proc_remove(proc_entry);
        printk(KERN_INFO "[%s] Removed /proc/%s\n", MODULE_NAME, PROC_ENTRY_NAME);
    }
    
    printk(KERN_INFO "[%s] Simplified Canary Trigger Module unloaded\n", MODULE_NAME);
}

module_init(canary_trigger_init);
module_exit(canary_trigger_exit);
//insmod /root/modules/canary_trigger.ko
//cat /proc/canary_test
