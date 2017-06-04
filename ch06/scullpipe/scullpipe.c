/*
 * scullpipe implements a blocking pipe, backed by a ring buffer.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define BUFFER_SIZE 16
static unsigned int buffer_size = BUFFER_SIZE;
module_param(buffer_size, uint, 0644);

static struct scullpipe_device {
	unsigned int buffer_size;
	unsigned int data_amount;
	char *buffer, *end;
	char *rp, *wp;
	wait_queue_head_t rq, wq;
	struct mutex mtx;
} sp_dev;

static void pr_buffer(void)
{
	unsigned int i;

	for (i = 0; i < sp_dev.buffer_size; i++)
		pr_debug("buffer[%u] = 0x%02x\n", i, sp_dev.buffer[i]);

	pr_debug("rp = %ld wp = %ld data_amount = %u\n",
			sp_dev.rp - sp_dev.buffer,
			sp_dev.wp - sp_dev.buffer,
			sp_dev.data_amount);
}

static ssize_t sp_read(struct file *filp, char __user *ubuf,
			size_t count, loff_t *offp)
{
	size_t size;

	mutex_lock(&sp_dev.mtx);

	while (sp_dev.data_amount == 0) {

		mutex_unlock(&sp_dev.mtx);

		if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(sp_dev.rq, sp_dev.data_amount != 0))
			return -ERESTARTSYS;

		mutex_lock(&sp_dev.mtx);

	}

	if (sp_dev.wp > sp_dev.rp)
		size = sp_dev.wp - sp_dev.rp;
	else
		size = sp_dev.end - sp_dev.rp;

	if (size > count)
		size = count;


	if (copy_to_user(ubuf, sp_dev.rp, size)) {
		mutex_unlock(&sp_dev.mtx);
		return -EFAULT;
	}

	sp_dev.data_amount -= size;
	sp_dev.rp += size;
	if (sp_dev.rp == sp_dev.end)
		sp_dev.rp = sp_dev.buffer;

	pr_buffer();

	mutex_unlock(&sp_dev.mtx);

	wake_up_interruptible(&sp_dev.wq);

	return size;
}

static ssize_t sp_write(struct file *filp, const char __user *ubuf,
			size_t count, loff_t *offp)
{
	size_t size;

	mutex_lock(&sp_dev.mtx);

	while (sp_dev.data_amount == sp_dev.buffer_size) {

		mutex_unlock(&sp_dev.mtx);

		if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(sp_dev.wq, sp_dev.data_amount != sp_dev.buffer_size))
			return -ERESTARTSYS;

		mutex_lock(&sp_dev.mtx);

	}

	if (sp_dev.wp < sp_dev.rp)
		size = sp_dev.rp - sp_dev.wp;
	else
		size = sp_dev.end - sp_dev.wp;

	if (size > count)
		size = count;

	if (copy_from_user(sp_dev.wp, ubuf, size)) {
		mutex_unlock(&sp_dev.mtx);
		return -EFAULT;
	}

	sp_dev.data_amount += size;
	sp_dev.wp += size;
	if (sp_dev.wp == sp_dev.end)
		sp_dev.wp = sp_dev.buffer;

	pr_buffer();

	mutex_unlock(&sp_dev.mtx);

	wake_up_interruptible(&sp_dev.rq);

	return size;
}

static const struct file_operations sp_fops = {
	.owner = THIS_MODULE,
	.read = sp_read,
	.write = sp_write,
};

static struct miscdevice sp_miscdevice = {
	.name = THIS_MODULE->name,
	.fops = &sp_fops,
};

static int __init sp_init(void)
{
	sp_dev.data_amount = 0;
	sp_dev.buffer_size = buffer_size;
	sp_dev.buffer = kzalloc(sp_dev.buffer_size, GFP_KERNEL);
	if (sp_dev.buffer <= 0)
		return -ENOMEM;

	sp_dev.rp = sp_dev.wp = sp_dev.buffer;
	sp_dev.end = sp_dev.buffer + buffer_size;

	pr_buffer();

	mutex_init(&sp_dev.mtx);
	init_waitqueue_head(&sp_dev.rq);
	init_waitqueue_head(&sp_dev.wq);

	return misc_register(&sp_miscdevice);
}

static void __exit sp_exit(void)
{
	misc_deregister(&sp_miscdevice);
	kfree(sp_dev.buffer);
}

module_init(sp_init);
module_exit(sp_exit);

MODULE_LICENSE("GPL");
