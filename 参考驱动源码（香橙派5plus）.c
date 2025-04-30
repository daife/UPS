#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/file.h>
#include <linux/err.h>
#include <linux/jiffies.h>  // 新增jiffies支持
#include <linux/platform_device.h>

#define DEVICE_PATH "/dev/ttyS4"  // UART
#define BAUDRATE 115200

static struct file *uart_file;
static struct task_struct *recv_thread;
static struct tty_struct *uart_tty;
static struct ktermios old_termios;
unsigned char buf[3];  // 双字节静态缓冲区

static struct power_supply *psy;
static struct device *virt_parent;  // 极简父设备

// 硬编码的电池属性

 int battery_capacity = 100;   // 电量：100%
 int battery_voltage = 16800000;  // 电压：16.8V（单位：uV）
 int battery_voltage_read =16800000;//capacity from reading,may have faults


static int custom_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{

    switch (psp) {
    case POWER_SUPPLY_PROP_CAPACITY:
        val->intval = battery_capacity;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = battery_voltage;
        break;
    case POWER_SUPPLY_PROP_STATUS:
        val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = 1;  // 电池存在
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static enum power_supply_property custom_props[] = {
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc custom_psy_desc = {
    .name = "UPS",
    .type = POWER_SUPPLY_TYPE_BATTERY,
    .properties = custom_props,
    .num_properties = ARRAY_SIZE(custom_props),
    .get_property = custom_get_property,
};

/* 内核线程接收函数 */
static int recv_data(void *data) {

    int bytes_needed = 3;  // 需要读取的字节数
    ssize_t read_size;
    struct file *filp = uart_file;
    void *cookie = NULL;
    unsigned long offset = 0;
    int marker_pos = -1;

    while (!kthread_should_stop()) {
        struct tty_ldisc *ld = tty_ldisc_ref(uart_tty);
      
        if (ld && ld->ops->read) {
            /* 直接读取到目标缓冲区剩余空间 */
            read_size = ld->ops->read(uart_tty, filp, 
                                    buf + (3 - bytes_needed), 
                                    bytes_needed, 
                                    &cookie, offset);
          
            if (read_size > 0) {
                bytes_needed -= read_size;
              
                /* 当凑满3字节时立即处理 */
                if (bytes_needed <= 0) {
                          printk(KERN_INFO "Received: 0x%02X 0x%02X 0x%02x\n", buf[0], buf[1], buf[2]);

                          // 查找0x93的位置
                          
                          for (int i = 0; i < 3; i++) {
                              if (buf[i] == 0x93) {
                                  marker_pos = i;
                                  break;
                              }
                          }
                          
                          if (marker_pos == -1) {
                              // 未找到标记，使用默认值
                              battery_voltage = 16800000;
                              printk(KERN_INFO "Marker 0x93 not found\n");
                          } else {
                              // 计算前两个有效字节的位置（循环）
                              int prev1 = (marker_pos - 1 + 3) % 3;
                              int prev2 = (marker_pos - 2 + 3) % 3;
                          
                              // 组合数据：前一位为高字节，再前一位为低字节
                              battery_voltage_read = ((buf[prev1] << 8) | buf[prev2]) * 1000;
                          
                              // 范围校验
                              if (battery_voltage_read > 12000000 && battery_voltage_read < 16800000) {
                                  battery_voltage = battery_voltage_read;
                              } else {
                                  battery_voltage = 16800000;
                              }
                          }
                   battery_capacity=(battery_voltage-12000000)*100/4800000;
                   power_supply_changed(psy); // 通知属性变化
                    bytes_needed = 3;  // 重置计数器
                    marker_pos=-1;

                }
            }
        }
        tty_ldisc_deref(ld);
      
        /* 无数据时短暂休眠（10ms） */
        if (bytes_needed == 3) 
            msleep(10);
    }
    return 0;
}
/* 配置串口参数 */
static int configure_uart(void) {
    struct ktermios kterm;
  
    kterm = uart_tty->termios;
  
    /* 设置波特率 */
    tty_termios_encode_baud_rate(&kterm, BAUDRATE, BAUDRATE);
  
    /* 8N1配置 */
    kterm.c_cflag &= ~PARENB;
    kterm.c_cflag &= ~CSTOPB;
    kterm.c_cflag &= ~CSIZE;
    kterm.c_cflag |= CS8;
    kterm.c_cflag |= CLOCAL | CREAD;
  
    /* 原始模式 */
    kterm.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    kterm.c_oflag &= ~OPOST;
  
    /* 设置超时和最小字符 */
    kterm.c_cc[VMIN]  = 0;
    kterm.c_cc[VTIME] = 10; // 1秒超时
  
    /* 应用配置 */
    tty_set_termios(uart_tty, &kterm);
    return 0;
}

static int __init custom_battery_init(void)
{
    int ret;

// 初始化父设备
virt_parent = kzalloc(sizeof(*virt_parent), GFP_KERNEL);
if (!virt_parent) {
    pr_err("Failed to allocate parent device\n");
    return -ENOMEM;
}
device_initialize(virt_parent);
dev_set_name(virt_parent, "virtual_battery_parent");

// 关键修改：关联到平台总线
//virt_parent->bus = &platform_bus_type;  // 让父设备挂载到 /sys/devices/platform/

// 注册父设备
ret = device_add(virt_parent);
if (ret < 0) {
    pr_err("Failed to add parent device: %d\n", ret);
    put_device(virt_parent);
    kfree(virt_parent);
    return ret;
}

psy = power_supply_register(virt_parent, &custom_psy_desc, NULL);
if (IS_ERR(psy)) {
    pr_err("Failed to register power supply: %ld\n", PTR_ERR(psy));
    device_del(virt_parent);
    put_device(virt_parent);
    kfree(virt_parent);
    return PTR_ERR(psy);
}

    printk(KERN_INFO "Initializing UART driver\n");
        /* 停止接收线程 */
        if (recv_thread) {
            kthread_stop(recv_thread);
            recv_thread = NULL;
        }
    
        /* 恢复原始配置 */
        if (uart_tty) {
            tty_set_termios(uart_tty, &old_termios);
        }
    
        /* 关闭设备 */
        if (uart_file) {
            filp_close(uart_file, NULL);
            uart_file = NULL;
        }



    /* 打开串口设备 */
    uart_file = filp_open(DEVICE_PATH, O_RDWR | O_NONBLOCK, 0);
    if (IS_ERR(uart_file)) {
        printk(KERN_ERR "Open %s failed: %ld\n", 
               DEVICE_PATH, PTR_ERR(uart_file));
        return PTR_ERR(uart_file);
    }

    /* 获取TTY结构体 */
    uart_tty = ((struct tty_file_private *)uart_file->private_data)->tty;
    if (!uart_tty) {
        printk(KERN_ERR "Failed to get TTY structure\n");
        filp_close(uart_file, NULL);
        return -ENODEV;
    }

    /* 保存原始配置 */
    old_termios = uart_tty->termios;
  
    /* 配置新参数 */
    if ((ret = configure_uart()) < 0) {
        filp_close(uart_file, NULL);
        return ret;
    }

    /* 创建接收线程 */
    recv_thread = kthread_run(recv_data, NULL, "uart_recv");
    if (IS_ERR(recv_thread)) {
        printk(KERN_ERR "Create thread failed: %ld\n", 
              PTR_ERR(recv_thread));
        filp_close(uart_file, NULL);
        return PTR_ERR(recv_thread);
    }

    printk(KERN_INFO "UART driver initialized\n");

    return 0;
}

static void __exit custom_battery_exit(void)
{
// 注销电源设备
power_supply_unregister(psy);

// 注销父设备
if (virt_parent) {
    device_del(virt_parent);   // 从sysfs移除
    put_device(virt_parent);   // 减少引用计数
    kfree(virt_parent);        // 释放内存
    virt_parent = NULL;
}

    printk(KERN_INFO "Unloading UART driver\n");

    /* 停止接收线程 */
    if (recv_thread) {
        kthread_stop(recv_thread);
        recv_thread = NULL;
    }

    /* 恢复原始配置 */
    if (uart_tty) {
        tty_set_termios(uart_tty, &old_termios);
    }

    /* 关闭设备 */
    if (uart_file) {
        filp_close(uart_file, NULL);
        uart_file = NULL;
    }

    printk(KERN_INFO "UART driver unloaded\n");
}

module_init(custom_battery_init);
module_exit(custom_battery_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dypho");
MODULE_DESCRIPTION("UPS with UART4");