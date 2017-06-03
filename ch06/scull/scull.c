#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include "scull.h"

static int scull_major = SCULL_MAJOR;
static int scull_minor;
static int scull_nr_devs = SCULL_NR_DEVS;
static int scull_quantum = SCULL_QUANTUM;
static int scull_qset = SCULL_QSET;

module_param(scull_major, int, 0);
module_param(scull_minor, int, 0);
module_param(scull_nr_devs, int, 0);
module_param(scull_quantum, int, 0);
module_param(scull_qset, int, 0);

struct scull_qset {
	void **data;
	struct scull_qset *next;
};

struct scull_dev {
	struct scull_qset *data;	// Pointer to first quantum set
	int quantum;			// the current quantum size
	int qset;			// the current array size
	unsigned long size;		// amount of data stored here
	unsigned int access_key;	// used by sculluid and scullpriv
	struct mutex mutex;		// mutual exclusion semaphore
	struct cdev cdev;		// Char device structure
};

static struct scull_dev *scull_devices;
static dev_t dev;

static struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

	// Allocate first qset explicitly if need be
	if (!qs) {
		qs = dev->data = kzalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;  /* Never mind */
	}

	// Then follow the list
	while (n--) {
		if (!qs->next) {
			qs->next = kzalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;  // Never mind
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

static ssize_t scull_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr; // the first listitem
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset; // how many bytes in the listitem
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (*f_pos >= dev->size)
		goto out;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	// find listitem, qset index, and offset in the quantum
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	// follow the list up to the right position (defined elsewhere)
	dptr = scull_follow(dev, item);

	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out; // don't fill holes
	// read only up to the end of this quantum
	if (count > quantum - q_pos)
		count = quantum - q_pos;
	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

out:
	mutex_unlock(&dev->mutex);
	return retval;
}

static ssize_t scull_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM; // value used in "goto out" statements

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	/// find listitem, qset index and offset in the quantum
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	// follow the list up to the right position
	dptr = scull_follow(dev, item);
	if (dptr == NULL)
		goto out;
	if (!dptr->data) {
		dptr->data = kcalloc(qset, sizeof(char *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
	}
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
		goto out;
	}
	// write only up to the end of this quantum
	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	// update the size
	if (dev->size < *f_pos)
		dev->size = *f_pos;
out:
	mutex_unlock(&dev->mutex);
	return retval;
}

static loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	struct scull_dev *dev = filp->private_data;
	loff_t newpos;

	switch(whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;

	case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	case 2: /* SEEK_END */
		newpos = dev->size + off;
		break;

	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

static void scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for (i = 0; i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
}

static int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;

	// trim to 0 the length of the device if open was write-only
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
		scull_trim(dev);

	return 0;
}

static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, ret = 0;
	struct scull_dev *dev = filp->private_data;

	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC || _IOC_NR(cmd) > SCULL_IOC_MAX_NR)
		return -ENOTTY;

	if ((_IOC_DIR(cmd) & _IOC_READ) == _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	if ((_IOC_DIR(cmd) & _IOC_WRITE) == _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch (cmd) {

	case SCULL_IOC_RESET:
		dev->quantum = scull_quantum;
		dev->qset = scull_qset;
		break;

	case SCULL_IOC_GET_QUANTUM:
		ret = __put_user(dev->quantum, (int __user *)arg);
		break;

	case SCULL_IOC_SET_QUANTUM:
		if (! capable(CAP_SYS_ADMIN))
			return -EPERM;
		ret = __get_user(dev->quantum, (int __user *)arg);
		break;

	case SCULL_IOC_GET_QSET:
		ret = __put_user(dev->qset, (int __user *)arg);
		break;

	case SCULL_IOC_SET_QSET:
		if (! capable(CAP_SYS_ADMIN))
			return -EPERM;
		ret = __get_user(dev->qset, (int __user *)arg);
		break;

	default:
		ret = -ENOTTY;

	}

	return ret;
}

static const struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.read = scull_read,
	.write = scull_write,
	.open = scull_open,
	.llseek = scull_llseek,
	.unlocked_ioctl = scull_ioctl,
};

static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	dev_t devno = MKDEV(scull_major, scull_minor + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;

	err = cdev_add(&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		pr_warn("Error %d adding scull%d", err, index);
}

static void scull_cleanup_module(void)
{
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Get rid of our char dev entries */
	if (scull_devices) {
		for (i = 0; i < scull_nr_devs; i++) {
			scull_trim(&scull_devices[i]);
			cdev_del(&(scull_devices[i].cdev));
		}
		kfree(scull_devices);
	}

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);
}

static int __init scull_init_module(void)
{
	int result, i;

	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs,
						THIS_MODULE->name);
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs,
						THIS_MODULE->name);
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		pr_warn("scull: can't get major %d\n", scull_major);
		return result;
	}

	scull_devices = kcalloc(scull_nr_devs,
				sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices) {
		scull_cleanup_module();
		return -ENOMEM;
	}

	// Initialize each device.
	for (i = 0; i < scull_nr_devs; i++) {
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset = scull_qset;
		mutex_init(&scull_devices[i].mutex);
		scull_setup_cdev(&scull_devices[i], i);
	}

	dev = MKDEV(scull_major, scull_minor + scull_nr_devs);

	return 0;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);

MODULE_LICENSE("GPL");
