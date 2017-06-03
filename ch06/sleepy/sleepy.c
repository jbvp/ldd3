/*
 * sleepy puts to sleep reader processes, and wake them up on write operations.
 *
 * Note that if multiple processes are sleeping, different scenarios might
 * happen when calling wake_up_interruptible:
 * - One process might quickly reach the line that set the flag to zero,
 *   thus immediately puting back to sleep the other processes.
 * - On the contrary, all or some processes might exit the
 *   wait_event_interrupitible function before any process sets flag to zero.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sched.h>

DECLARE_WAIT_QUEUE_HEAD(wq);
static int flag;

static ssize_t sleepy_read(struct file *filp, char __user *ubuf,
			size_t count, loff_t *offp)
{
	pr_debug("Process %i (%s) is going to sleep\n",
		current->pid, current->comm);

	wait_event_interruptible(wq, flag != 0);
	flag = 0;

	pr_debug("Process %i (%s) woke up\n", current->pid, current->comm);
	return 0;
}

static ssize_t sleepy_write(struct file *filp, const char __user *ubuf,
			size_t count, loff_t *offp)
{
	pr_debug("Process %i (%s) wake up the readers...\n",
		current->pid, current->comm);

	flag = 1;
	wake_up_interruptible(&wq);

	return count;
}

static const struct file_operations sleepy_fops = {
	.owner = THIS_MODULE,
	.read = sleepy_read,
	.write = sleepy_write,
};

static struct miscdevice sleepy_miscdevice = {
	.name = THIS_MODULE->name,
	.fops = &sleepy_fops,
};

static int __init sleepy_init(void)
{
	return misc_register(&sleepy_miscdevice);
}

static void __exit sleepy_exit(void)
{
	misc_deregister(&sleepy_miscdevice);
}

module_init(sleepy_init);
module_exit(sleepy_exit);
