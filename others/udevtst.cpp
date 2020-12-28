#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#include <libudev.h>

static int show_device_action(struct udev_device *device)
{
    const char *action, *str;

    action = udev_device_get_action(device);
    if (action == NULL) {
        printf("No action got\n");
        return 0;
    }

    printf("@@ [%s] Action=%s\n", __func__, action);

    str = udev_device_get_syspath(device);
    if (str != NULL)
        printf("syspath: %s\n", str);

    str = udev_device_get_subsystem(device);
    if (str != NULL)
        printf("subsystem: %s\n", str);

    str = udev_device_get_sysname(device);
    if (str != NULL)
        printf("sysname: %s\n", str);

    str = udev_device_get_devpath(device);
    if (str != NULL)
        printf("devpath: %s\n", str);

    str = udev_device_get_devtype(device);
    if (str != NULL)
        printf("devtype: %s\n", str);

    str = udev_device_get_driver(device);
    if (str != NULL)
        printf("driver: %s\n", str);

    str = udev_device_get_devnode(device);
    if (str != NULL)
        printf("devnode: %s\n", str);

    {
        struct udev_list_entry *list_entry;

                udev_list_entry_foreach(list_entry, udev_device_get_properties_list_entry(device)) {
                        if (!udev_list_entry_get_name(list_entry))
                                continue;
                        printf("E:%s=%s\n",
                   udev_list_entry_get_name(list_entry),
                   udev_list_entry_get_value(list_entry));
                }
    }

    return 0;
}

int main ()
{
    struct udev *udev;
    struct udev_monitor *udev_monitor = NULL;
    int ret, fdcount;
    fd_set readfds;
    struct udev_device *device;
//	const char *syspath;

    udev = udev_new();
    if (udev == NULL) {
        printf("Error: udev_new\n");
        return 1;
    }


    udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (udev_monitor == NULL) {
        printf("Error: udev_monitor_new_from_netlink\n");
        return 1;
    }

    /* Add some filter */
    ret = udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "tty",0);
    if (ret < 0) {
        printf("Error: filter_add_match_subsystem_devtype\n");
        return 1;
    }

    ret = udev_monitor_enable_receiving(udev_monitor);
    if (ret < 0) {
        printf("udev_monitor_enable_receiving Failed < 0\n");
        return 1;
    }

    /* Event Loop */
    while (1) {
        FD_ZERO(&readfds);

        if (udev_monitor != NULL) {
            FD_SET(udev_monitor_get_fd(udev_monitor), &readfds);
        }

        fdcount = select(udev_monitor_get_fd(udev_monitor) + 1,
                 &readfds, NULL, NULL, NULL);
        if (fdcount < 0) {
            if (errno != EINTR) {
                printf("Receive Signal!!\n");
                return 1;
            }
            continue;
        }

        if (udev_monitor == NULL) {
            printf("@@ udev_monitor is NULL(check1)\n");
            continue;
//			return -1;
        }

        if (!FD_ISSET(udev_monitor_get_fd(udev_monitor), &readfds)) {
            continue;
        }

        device = udev_monitor_receive_device(udev_monitor);
        if (device == NULL) {
            printf("@@ receive device is not found(check2)\n");
            continue;
        }
        show_device_action(device);
    }

    return 0;
}
