#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/err.h>
#include <linux/rtnetlink.h>
#include <linux/wait.h>

#include "mgcap_ring.h"
#include "mgcap_rx.h"
#include "mgcap.h"

#define MGCAP_VERSION  "0.0.0"

/* Module parameters, defaults. */
static int debug = 0;
static char *ifname = "eth0";
static int rxbuf_size = 19;

/* Global variables */
static struct mgc_dev *mgc;


static int mgcap_open(struct inode *, struct file *);
static int mgcap_release(struct inode *, struct file *);
static unsigned int mgcap_poll(struct file *, poll_table *);
static long mgcap_ioctl(struct file *, unsigned int, unsigned long);
static int __init mgcap_init_module(void);
static void mgcap_exit(void);
static void __exit mgcap_exit_module(void);
static ssize_t mgcap_read(struct file *, char __user *, size_t, loff_t *);


static struct file_operations mgcap_fops = {
	.owner = THIS_MODULE,
	.open = mgcap_open,
	.read = mgcap_read,     // tmp
	.poll = mgcap_poll,
	.unlocked_ioctl = mgcap_ioctl,
	.release = mgcap_release,
};

static struct miscdevice mgcap_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRV_NAME,
	.fops = &mgcap_fops,
};


static int
mgcap_open(struct inode *inode, struct file *filp)
{
	pr_info("entering %s\n", __func__);
	return 0;
}

static int
mgcap_release(struct inode *inode, struct file *filp)
{
	pr_info("entering %s\n", __func__);
	return 0;
}

static unsigned int
mgcap_poll(struct file* filp, poll_table* wait)
{
	pr_info("entering %s\n", __func__);
	return 0;
}

static ssize_t
mgcap_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	int copy_len, available_read_len;
	struct mgc_ring *rx = &mgc->rx[0].buf;

	if (ring_empty(rx))
		return 0;

	available_read_len = ring_free_count(rx);
	if (count > available_read_len)
		copy_len = available_read_len;
	else
		copy_len = count;

	if (copy_to_user(buf, rx->read, copy_len)) {
		pr_info("copy_to_user failed\n");
		return -EFAULT;
	}

	ring_read_next(rx, copy_len);

	return copy_len;
}

static long
mgcap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
#if 0
	struct mgc_ring txbuf, rxbuf;

	pr_info("entering %s\n", __func__);

	switch(cmd) {
		case MGCTXSYNC:
			copy_from_user(&txbuf, (void *)arg, sizeof(struct mgc_ring));
			pr_info("MGCTXSYNC: txbuf->head: %p", txbuf.head);
			break;
		case MGCRXSYNC:
			copy_from_user(&rxbuf, (void *)arg, sizeof(struct mgc_ring));
			pr_info("MGCRXSYNC: rxbuf->head: %p", rxbuf.head);
			break;
		default:
			break;
	}
#endif
	return  ret;
}

static int __init
mgcap_init_module(void)
{
	unsigned int malloc_ring_size;
	char pathdev[IFNAMSIZ];
	int rc, cpu, i;

	pr_info("mgcap (v%s) is loaded\n", MGCAP_VERSION);

	/* malloc mgc_dev */
	mgc = kmalloc(sizeof(struct mgc_dev), GFP_KERNEL);
	if (mgc == 0) {
		pr_err("fail to kmalloc: *mgc_dev\n");
		goto err;
	}

	mgc->num_cpus = num_online_cpus();
	pr_info("mgc->num_cpus: %d\n", mgc->num_cpus);

	/* malloc mgc_dev->rx */
	mgc->rx = kmalloc(sizeof(struct rxring) * mgc->num_cpus, GFP_KERNEL);
	if (mgc->rx == 0) {
		pr_err("fail to kmalloc: *mgc_dev->rx\n");
		goto err;
	}
	
	/* malloc mgc_dev->rx->buf */
	i = 0;
	for_each_online_cpu(cpu) {
		mgc->rx[i].cpuid = cpu;

		malloc_ring_size = RING_SIZE + MAX_PKT_SIZE * NBULK_PKT;
		if ((mgc->rx[i].buf.start = kmalloc(malloc_ring_size, GFP_KERNEL)) == 0) {
			pr_err("fail to kmalloc: *mgc_dev->rx[%d].buf, cpu=%d\n", i, cpu);
			goto err;
		}
		mgc->rx[i].buf.end   = mgc->rx[i].buf.start + RING_SIZE - 1;
		mgc->rx[i].buf.write = mgc->rx[i].buf.start;
		mgc->rx[i].buf.read  = mgc->rx[i].buf.start;
		mgc->rx[i].buf.mask  = RING_SIZE - 1;
		pr_info("cpu=%d, rxbuf[%d], st: %p, wr: %p, rd: %p, end: %p\n",
			cpu, i,
			mgc->rx[i].buf.start, mgc->rx[i].buf.write,
			mgc->rx[i].buf.read,  mgc->rx[i].buf.end);

		++i;
	}

	/* mgc->dev */
	mgc->dev = dev_get_by_name(&init_net, ifname);
	if (!mgc->dev) {
		pr_err("Could not find %s\n", ifname);
		goto err;
	}

	/* register character device */
	sprintf(pathdev, "%s/%s", DRV_NAME, ifname);
	mgcap_dev.name = pathdev;
	rc = misc_register(&mgcap_dev);
	if (rc) {
		pr_err("fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		goto err;
	}

	// netdev_rx_handler_register
	rtnl_lock();
	rc = netdev_rx_handler_register(mgc->dev, mgc_handle_frame, mgc);
	rtnl_unlock();
	if (rc) {
		pr_err("%s failed to register rx_handler\n", ifname);
		goto err;
	}

	return rc;

err:
	mgcap_exit();
	return -1;
}
module_init(mgcap_init_module);


void
mgcap_exit(void)
{
	int i;

	if (mgc->dev) {
		rtnl_lock();
		netdev_rx_handler_unregister(mgc->dev);
		rtnl_unlock();
	}

	misc_deregister(&mgcap_dev);

	/* free rx ring buffer */
	for (i = 0; i < mgc->num_cpus; i++) {
		if (mgc->rx[i].buf.start) {
			kfree(mgc->rx[i].buf.start);
			mgc->rx[i].buf.start = NULL;
		}
	}

	/* free rx ring buffers */
	if (mgc->rx) {
		kfree(mgc->rx);
		mgc->rx = NULL;
	}

	if (mgc) {
		kfree(mgc);
		mgc = NULL;
	}
}

static void __exit
mgcap_exit_module(void)
{
	pr_info("mgcap (v%s) is unloaded\n", MGCAP_VERSION);

	mgcap_exit();

	return;
}
module_exit(mgcap_exit_module);

MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("MGCAP device");
MODULE_LICENSE("GPL");
MODULE_VERSION(MGCAP_VERSION);

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug flag");
module_param(ifname, charp, S_IRUGO);
MODULE_PARM_DESC(ifname, "Target network device name");
module_param(rxbuf_size, int, S_IRUGO);
MODULE_PARM_DESC(rxbuf_size, "RX ring size on each received packet (1<<rxbuf_size)");

