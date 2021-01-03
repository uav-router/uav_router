#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <malloc.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <string>

#define NLINK_MSG_LEN 16 * 1024


std::map<int,std::string> names = {
    {IFLA_UNSPEC,"IFLA_UNSPEC"},
	{IFLA_ADDRESS,"IFLA_ADDRESS"},
	{IFLA_BROADCAST,"IFLA_BROADCAST"},
	{IFLA_IFNAME,"IFLA_IFNAME"},
	{IFLA_MTU,"IFLA_MTU"},
	{IFLA_LINK,"IFLA_LINK"},
	{IFLA_QDISC,"IFLA_QDISC"},
	{IFLA_STATS,"IFLA_STATS"},
	{IFLA_COST,"IFLA_COST"},
	{IFLA_PRIORITY,"IFLA_PRIORITY"},
	{IFLA_MASTER,"IFLA_MASTER"},
	{IFLA_WIRELESS,"IFLA_WIRELESS"},
	{IFLA_PROTINFO,"IFLA_PROTINFO"},
	{IFLA_TXQLEN,"IFLA_TXQLEN"},
	{IFLA_MAP,"IFLA_MAP"},
	{IFLA_WEIGHT,"IFLA_WEIGHT"},
	{IFLA_OPERSTATE,"IFLA_OPERSTATE"},
	{IFLA_LINKMODE,"IFLA_LINKMODE"},
	{IFLA_LINKINFO,"IFLA_LINKINFO"},
	{IFLA_NET_NS_PID,"IFLA_NET_NS_PID"},
	{IFLA_IFALIAS,"IFLA_IFALIAS"},
	{IFLA_NUM_VF,"IFLA_NUM_VF"},
	{IFLA_VFINFO_LIST,"IFLA_VFINFO_LIST"},
	{IFLA_STATS64,"IFLA_STATS64"},
	{IFLA_VF_PORTS,"IFLA_VF_PORTS"},
	{IFLA_PORT_SELF,"IFLA_PORT_SELF"},
	{IFLA_AF_SPEC,"IFLA_AF_SPEC"},
	{IFLA_GROUP,"IFLA_GROUP"},
	{IFLA_NET_NS_FD,"IFLA_NET_NS_FD"},
	{IFLA_EXT_MASK,"IFLA_EXT_MASK"},
	{IFLA_PROMISCUITY,"IFLA_PROMISCUITY"},
	{IFLA_NUM_TX_QUEUES,"IFLA_NUM_TX_QUEUES"},
	{IFLA_NUM_RX_QUEUES,"IFLA_NUM_RX_QUEUES"},
	{IFLA_CARRIER,"IFLA_CARRIER"},
	{IFLA_PHYS_PORT_ID,"IFLA_PHYS_PORT_ID"},
	{IFLA_CARRIER_CHANGES,"IFLA_CARRIER_CHANGES"},
	{IFLA_PHYS_SWITCH_ID,"IFLA_PHYS_SWITCH_ID"},
	{IFLA_LINK_NETNSID,"IFLA_LINK_NETNSID"},
	{IFLA_PHYS_PORT_NAME,"IFLA_PHYS_PORT_NAME"},
	{IFLA_PROTO_DOWN,"IFLA_PROTO_DOWN"},
	{IFLA_GSO_MAX_SEGS,"IFLA_GSO_MAX_SEGS"},
	{IFLA_GSO_MAX_SIZE,"IFLA_GSO_MAX_SIZE"},
	{IFLA_PAD,"IFLA_PAD"},
	{IFLA_XDP,"IFLA_XDP"},
	{IFLA_EVENT,"IFLA_EVENT"},
	{IFLA_NEW_NETNSID,"IFLA_NEW_NETNSID"},
	{IFLA_IF_NETNSID,"IFLA_IF_NETNSID"},
	{IFLA_TARGET_NETNSID,"IFLA_TARGET_NETNSID"},
	{IFLA_CARRIER_UP_COUNT,"IFLA_CARRIER_UP_COUNT"},
	{IFLA_CARRIER_DOWN_COUNT,"IFLA_CARRIER_DOWN_COUNT"},
	{IFLA_NEW_IFINDEX,"IFLA_NEW_IFINDEX"},
	{IFLA_MIN_MTU,"IFLA_MIN_MTU"},
	{IFLA_MAX_MTU,"IFLA_MAX_MTU"},
	{IFLA_PROP_LIST,"IFLA_PROP_LIST"},
	{IFLA_ALT_IFNAME,"IFLA_ALT_IFNAME"},
	{IFLA_PERM_ADDRESS,"IFLA_PERM_ADDRESS"}
};


void rtnl_print_link(struct nlmsghdr *h) {
  struct rtattr *attribute;
  int len;

  ifinfomsg *iface = (ifinfomsg *)NLMSG_DATA(h);
  len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*iface));
  printf("Interface %d\n", iface->ifi_index);
  /* loop over all attributes for the NEWLINK message */
  for (attribute = IFLA_RTA(iface); RTA_OK(attribute, len); attribute = RTA_NEXT(attribute, len)) {
    switch (attribute->rta_type) {
    case IFLA_IFNAME:
      printf("Interface name : %s\n", (char *)RTA_DATA(attribute));
      break;
    default:
      printf("Attribute %i %s\n",attribute->rta_type,names[attribute->rta_type].c_str());
      break;
    }
  }
}

int main() {
  int fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  printf("Inside recv main\n");

  if (fd < 0) {
    printf("Socket creation failed. try again\n");
    return -1;
  }

  struct sockaddr_nl src_addr;
  struct sockaddr_nl dest_addr;
  // allocate buffer for netlink message which
  // is message header + message payload
  struct nlmsghdr *nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(NLINK_MSG_LEN));
  // fill the iovec structure
  struct iovec iov;
  // define the message header for message
  struct msghdr msg;

  nlh->nlmsg_len = NLMSG_SPACE(NLINK_MSG_LEN);
  nlh->nlmsg_pid = 1000; // src application unique id
  nlh->nlmsg_flags = 0;

  src_addr.nl_family = AF_NETLINK;  // AF_NETLINK socket protocol
  src_addr.nl_pid = 2;              // application unique id
  src_addr.nl_groups = RTMGRP_LINK; // specify not a multicast communication

  // attach socket to unique id or address
  (void)bind(fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

  iov.iov_base = (void *)nlh;   // netlink message header base address
  iov.iov_len = nlh->nlmsg_len; // netlink message length

  msg.msg_name = (void *)&dest_addr;
  msg.msg_namelen = sizeof(dest_addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  /* Listen forever in a while loop */
  struct nlmsghdr *nh;
  while (1) {
    // receive the message
    int len = recvmsg(fd, &msg, 0);
    printf("read %i bytes\n", len);
    for (nh = nlh; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
      /* The end of multipart message */
      if (nh->nlmsg_type == NLMSG_DONE)
        break;

      if (nh->nlmsg_type == NLMSG_ERROR) {
        printf("error message\n");
        continue;
      }
      if (nh->nlmsg_type == RTM_NEWLINK) {
        printf("new link\n");
        rtnl_print_link(nh);
        continue;
      }
      if (nh->nlmsg_type == RTM_DELLINK) {
        printf("del link\n");
        rtnl_print_link(nh);
        continue;
      }
      printf("some message %i\n", nh->nlmsg_type);
    }
  }
  close(fd); // close the socket
}
