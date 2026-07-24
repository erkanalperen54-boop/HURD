/*
 * Copyright (C) 2024 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if_ether.h>
#include <error.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include <mach.h>
#include <mach/gnumach.h>
#include <mach/vm_param.h>
#include <hurd/machdev.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <device/net_status.h>
#include <net/ethernet.h>

#define MACH_INCLUDE
#define _STANDALONE

#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumperrno2host.h>

#include "if_hdr.h"
#include "ioccom-rump.h"

#define	SIOCSIFADDR	 _IOW('i', 12, struct ifreq)	/* set ifnet address */
#define	SIOCGIFADDR	_IOWR('i', 33, struct ifreq)	/* get ifnet address */
#define	SIOCGIFBRDADDR	_IOWR('i', 35, struct ifreq)	/* get broadcast addr */
#define	SIOCSIFBRDADDR	 _IOW('i', 19, struct ifreq)	/* set broadcast addr */
#define	SIOCGIFNETMASK	_IOWR('i', 37, struct ifreq)	/* get net addr mask */
#define	SIOCSIFNETMASK	 _IOW('i', 22, struct ifreq)	/* set net addr mask */

#define SIOCGLIFADDR    _IOWR('i', 29, struct if_laddrreq) /* get IF addr */

#define	SIOCSIFFLAGS	 _IOW('i', 16, struct ifreq)	/* set ifnet flags */
#define	SIOCGIFFLAGS	_IOWR('i', 17, struct ifreq)	/* get ifnet flags */
#define	SIOCGIFMETRIC	_IOWR('i', 23, struct ifreq)	/* get IF metric */
#define	SIOCSIFMETRIC	 _IOW('i', 24, struct ifreq)	/* set IF metric */
#define	SIOCSIFMTU	 _IOW('i', 127, struct ifreq)	/* set ifnet mtu */
#define	SIOCGIFMTU	_IOWR('i', 126, struct ifreq)	/* get ifnet mtu */

#define IF_NAMESIZE	16

#define NET_RCV_WAIT	(60*1000*1000) /* 1 minute */
#define RUMP_POLLIN	0x0001
struct pollfd {
  int fd;
  short events;
  short revents;
};

struct rump_timeval {
  int64_t tv_sec;
  int32_t tv_usec;
};

struct bpf_insn;

/*
 *  Structure for BIOCSETF.
 */
struct bpf_program {
	u_int bf_len;
	struct bpf_insn *bf_insns;
};

#define BIOCGBLEN	 _IOR('B', 102, u_int)
#define BIOCSBLEN	_IOWR('B', 102, u_int)
#define BIOCSETF	 _IOW('B', 103, struct bpf_program)
#define BIOCFLUSH	  _IO('B', 104)
#define BIOCPROMISC	  _IO('B', 105)
#define BIOCGDLT	 _IOR('B', 106, u_int)
#define BIOCGETIF	 _IOR('B', 107, struct ifreq)
#define BIOCSETIF	 _IOW('B', 108, struct ifreq)
#define BIOCIMMEDIATE	 _IOW('B', 112, u_int)
#define BIOCSTCPF	 _IOW('B', 114, struct bpf_program)
#define BIOCSUDPF	 _IOW('B', 115, struct bpf_program)
#define BIOCGHDRCMPLT	 _IOR('B', 116, u_int)
#define BIOCSHDRCMPLT	 _IOW('B', 117, u_int)
#define BIOCSDLT	 _IOW('B', 118, u_int)
#define BIOCGDIRECTION	 _IOR('B', 120, u_int)
#define BIOCSDIRECTION	 _IOW('B', 121, u_int)
#define BIOCSRTIMEOUT    _IOW('B', 122, struct rump_timeval)
#define BIOCGFEEDBACK	 _IOR('B', 124, u_int)
#define BIOCSFEEDBACK	 _IOW('B', 125, u_int)
#define BIOCLOCK	  _IO('B', 126)
#define BIOCSETWF	 _IOW('B', 127, struct bpf_program)

#define BPF_D_IN		0
#define BPF_D_INOUT		1
#define BPF_D_OUT		2

/*
 * Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST).
 */
#define	BPF_MEMWORDS		16

#define SIZEOF_BPF_HDR		18
#define SIZEOF_MTU		1500
#define SIZEOF_ETH_FRAME	(ETH_HLEN + SIZEOF_MTU)

#define BPF_WORDALIGN(x) (((x)+(BPF_ALIGNMENT-1))&~(BPF_ALIGNMENT-1))

#define BPF_BUFSIZE	131072

struct bpf_hdr {
#if defined (__x86_64__)
  int64_t usec;
  int64_t sec;
#else
  int32_t usec;
  int32_t sec;
#endif
  uint32_t bh_caplen;	/* length of captured portion */
  uint32_t bh_datalen;	/* original length of packet */
  uint16_t bh_hdrlen;	/* length of this struct + alignment padding */
};

int socket_aflink = -1;

struct dl_addr {
  uint8_t     dl_type;    /* interface type */
  uint8_t     dl_nlen;    /* interface name length, no trailing 0 reqd. */
  uint8_t     dl_alen;    /* link level address length */
  uint8_t     dl_slen;    /* link layer selector length */
  char        dl_data[24]; /*
                            * minimum work area, can be larger; contains
                            * both if name and ll address; big enough for
                            * IFNAMSIZ plus 8byte ll addr.
                            */
};

struct sockaddr_dl {
  uint8_t     sdl_len;    /* Total length of sockaddr */
  uint8_t     sdl_family; /* AF_LINK */
  uint16_t    sdl_index;  /* if != 0, system given index for interface */
  struct dl_addr sdl_addr;
#define sdl_type        sdl_addr.dl_type
#define sdl_nlen        sdl_addr.dl_nlen
#define sdl_alen        sdl_addr.dl_alen
#define sdl_slen        sdl_addr.dl_slen
#define sdl_data        sdl_addr.dl_data
};

struct if_laddrreq {
  char iflr_name[IF_NAMESIZE];
  unsigned int flags;
#define IFLR_PREFIX     0x8000  /* in: prefix given  out: kernel fills id */
#define IFLR_ACTIVE     0x4000  /* in/out: link-layer address activation */
#define IFLR_FACTORY    0x2000  /* in/out: factory link-layer address */
  unsigned int prefixlen;         /* in/out */
  struct sockaddr_storage addr;   /* in/out */
  struct sockaddr_storage dstaddr; /* out */
};

#define satosdl(__sa)   ((struct sockaddr_dl *)(__sa))
#define LLADDR(s) ((char *)((s)->sdl_data + (s)->sdl_nlen))

struct ifreq {
  char    ifr_name[IF_NAMESIZE];
  union {
    struct  sockaddr ifru_addr;
    struct  sockaddr ifru_dstaddr;
    struct  sockaddr ifru_broadaddr;
    struct  sockaddr_storage ifru_space;
    short   ifru_flags;
    int     ifru_addrflags;
    int     ifru_metric;
    int     ifru_mtu;
    int     ifru_dlt;
    u_int   ifru_value;
    void *  ifru_data;
    struct {
      uint32_t b_buflen;
      void     *b_buf;
    } ifru_b;
  } ifr_ifru;
#define ifr_addr        ifr_ifru.ifru_addr      /* address */
#define ifr_dstaddr     ifr_ifru.ifru_dstaddr   /* other end of p-to-p link */
#define ifr_broadaddr   ifr_ifru.ifru_broadaddr /* broadcast address */
#define ifr_space       ifr_ifru.ifru_space     /* sockaddr_storage */
#define ifr_flags       ifr_ifru.ifru_flags     /* flags */
#define ifr_addrflags   ifr_ifru.ifru_addrflags /* addr flags */
#define ifr_metric      ifr_ifru.ifru_metric    /* metric */
#define ifr_mtu         ifr_ifru.ifru_mtu       /* mtu */
#define ifr_dlt         ifr_ifru.ifru_dlt       /* data link type (DLT_*) */
#define ifr_value       ifr_ifru.ifru_value     /* generic value */
#define ifr_media       ifr_ifru.ifru_metric    /* media options (overload) */
#define ifr_data        ifr_ifru.ifru_data      /* for use by interface
                                                 * XXX deprecated
                                                 */
#define ifr_buf         ifr_ifru.ifru_b.b_buf   /* new interface ioctls */
#define ifr_buflen      ifr_ifru.ifru_b.b_buflen
#define ifr_index       ifr_ifru.ifru_value     /* interface index, BSD */
#define ifr_ifindex     ifr_index               /* interface index, linux */
};

struct bpf_insn bpf_allow_all[] = {
  BPF_STMT(BPF_RET+BPF_K, BPF_BUFSIZE),	/* accept */
};

/* One of these is associated with each instance of a device.  */
struct net_data
{
  struct port_info port;	/* device port */
  struct machdev_emul_device device;	/* generic device structure */
  struct ifreq *dev;	/* rump network device structure */
  uint8_t hw_address[ETH_ALEN]; /* MAC address of device */
  mach_port_t dest;		/* destination port for recieving packets */
  int bpf_fd;			/* bpf file descriptor for communication with device */
  struct net_data *next;
};

static struct net_data *nd_head;

/* Forward declarations.  */

static void *rcv_process (void *arg);

static struct machdev_device_emulation_ops rump_net_emulation_ops;

static mach_msg_type_t header_type =
{
  .msgt_name = MACH_MSG_TYPE_BYTE,
  .msgt_size = 8,
  .msgt_number = NET_HDW_HDR_MAX,
  .msgt_inline = TRUE,
  .msgt_longform = FALSE,
  .msgt_deallocate = FALSE,
  .msgt_unused = 0
};

static mach_msg_type_t packet_type =
{
  .msgt_name = MACH_MSG_TYPE_BYTE,
  .msgt_size = 8,
  .msgt_number = 0,
  .msgt_inline = TRUE,
  .msgt_longform = FALSE,
  .msgt_deallocate = FALSE
};

static struct net_data *search_nd (struct ifreq *dev)
{
  struct net_data *nd = nd_head;

  while (nd)
    {
      if (strncmp(nd->dev->ifr_name, dev->ifr_name, IF_NAMESIZE) == 0)
	return nd;
      nd = nd->next;
    }
  return NULL;
}

/* Return a send right associated with network device ND.  */
static mach_port_t
dev_to_port (void *nd)
{
  return (nd
	  ? ports_get_send_right (nd)
	  : MACH_PORT_NULL);
}

void socket_init(void)
{
  int err;

  socket_aflink = rump_sys_socket(RUMP_AF_LINK, SOCK_DGRAM, 0);
  if (socket_aflink < 0)
    mach_print("ERROR rump_sys_socket(RUMP_AF_LINK)\n");
}

static int
get_hwaddr(const char *ifname, uint8_t *mac)
{
  struct if_laddrreq iflr;
  struct sockaddr_dl *sdl;

  memset(&iflr, 0, sizeof(iflr));
  strlcpy(iflr.iflr_name, ifname, sizeof(iflr.iflr_name));
  iflr.addr.ss_family = RUMP_AF_LINK;

  sdl = satosdl(&iflr.addr);
  sdl->sdl_alen = ETH_ALEN;

  if (rump_sys_ioctl(socket_aflink, SIOCGLIFADDR, &iflr) == -1)
    {
      mach_print("ERROR siocglifaddr failed\n");
      return -1;
    }

  memcpy(mac, LLADDR(sdl), ETH_ALEN);
  return 0;
}

static int
cmp_hwaddr(uint8_t *hwaddr, uint8_t *devaddr)
{
  return memcmp(hwaddr, devaddr, ETH_ALEN);
}

static int
cmp_hwbroadcast(uint8_t *hw)
{
  uint8_t all[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  return memcmp(hw, all, ETH_ALEN);
}

struct ifreq *
search_interface(const char *ifname)
{
  struct ifreq ifr;
  struct ifreq *dev = NULL;
  const char *last_slash, *name;

  memset(&ifr, 0, sizeof(ifr));
  last_slash = strrchr(ifname, '/');
  if (!last_slash)
    name = ifname;
  else
    name = last_slash + 1;
  dev = malloc(sizeof(*dev));
  if (dev == NULL)
    {
      mach_print("ERROR: cannot malloc\n");
      return NULL;
    }
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  if (rump_sys_ioctl(socket_aflink, SIOCGIFFLAGS, &ifr) == -1)
    {
      mach_print("siocgifflags failed: ");
      mach_print(name);
      mach_print("\n");
      goto errexit;
    }
  *dev = ifr;
  goto exit;

errexit:
  free(dev);
  dev = NULL;
exit:
  return dev;
}

static io_return_t
init_interface(struct net_data *nd)
{
  unsigned int flag, buf_size;
  io_return_t err;
  struct bpf_program p;
  pthread_t rcv_thread;
  /* use minimal 1 microsecond timeout, (0 does not work) */
  struct rump_timeval timeout = { 0, 1 };

  if (get_hwaddr(nd->dev->ifr_name, nd->hw_address))
    {
      mach_print("ERROR can't get mac address\n");
      return rump_errno2host(errno);
    }

  /* Hardcode MTU to 1500 */
  nd->dev->ifr_mtu = SIZEOF_MTU;
  if (rump_sys_ioctl(socket_aflink, SIOCSIFMTU, nd->dev) == -1)
    {
      mach_print("ERROR siocsifmtu\n");
      return rump_errno2host(errno);
    }

  nd->bpf_fd = rump_sys_open("/dev/bpf", RUMP_O_RDWR);
  if (nd->bpf_fd < 0)
    {
      mach_print("ERROR rump_sys_open(/dev/bpf)\n");
      return rump_errno2host(errno);
    }

  buf_size = BPF_BUFSIZE;
  /* ignore return value */
  rump_sys_ioctl (nd->bpf_fd, BIOCSBLEN, &buf_size);

  err = rump_sys_ioctl (nd->bpf_fd, BIOCSETIF, nd->dev);
  if (err < 0)
    {
      mach_print("ERROR: biocsetif failed, buf_size too big?\n");
      errno = rump_errno2host(err);
      return errno;
    }

  flag = 0;
  err = rump_sys_ioctl (nd->bpf_fd, BIOCIMMEDIATE, &flag);
  if (err < 0)
    {
      mach_print("ERROR: biocimmediate failed\n");
      errno = rump_errno2host(err);
      return errno;
    }

  /* We need this timeout for blocking requests to flush even if not full */
  err = rump_sys_ioctl (nd->bpf_fd, BIOCSRTIMEOUT, &timeout);
  if (err < 0)
    {
      mach_print("ERROR: biocsrtimeout failed\n");
      errno = rump_errno2host(err);
      return errno;
    }

  /* only capture incoming packets, but still allows sending packets */
  flag = BPF_D_IN;
  err = rump_sys_ioctl (nd->bpf_fd, BIOCSDIRECTION, &flag);
  if (err < 0)
    {
      mach_print("ERROR: biocsdirection failed\n");
      errno = rump_errno2host(err);
      return -1;
    }

  p.bf_len = sizeof(bpf_allow_all) / sizeof(bpf_allow_all[0]);
  p.bf_insns = bpf_allow_all;

  err = rump_sys_ioctl (nd->bpf_fd, BIOCSETF, &p);
  if (err < 0)
    {
      mach_print("ERROR: biocsetf failed\n");
      errno = rump_errno2host(err);
      return errno;
    }

  err = pthread_create(&rcv_thread, 0, rcv_process, nd);
  if (err != 0)
    {
      mach_print("ERROR: pthread_create(rcv)\n");
      return err;
    }
  pthread_detach(rcv_thread);

  return 0;
}

int
up_interface(struct ifreq *dev)
{
  int retval;

  if (rump_sys_ioctl(socket_aflink, SIOCGIFFLAGS, dev) == 0)
    {
      if ((dev->ifr_flags & IFF_UP))
        retval = 0;
      else
        {
          dev->ifr_flags |= IFF_UP | IFF_RUNNING;
          if (rump_sys_ioctl(socket_aflink, SIOCSIFFLAGS, dev) == 0)
            retval = 0;
	  else
	    retval = rump_errno2host(errno);
  	}
    }
  else
    retval = rump_errno2host(errno);

  return retval;
}

static io_return_t
deliver_msg(struct net_rcv_msg *msg, mach_port_t p)
{
  mach_msg_return_t err;

  msg->msg_hdr.msgh_bits = MACH_MSGH_BITS (MACH_MSG_TYPE_COPY_SEND, 0);
  /* remember message sizes must be rounded up */
  msg->msg_hdr.msgh_local_port = MACH_PORT_NULL;
  msg->msg_hdr.msgh_kind = MACH_MSGH_KIND_NORMAL;
  msg->msg_hdr.msgh_id = NET_RCV_MSG_ID;

  msg->msg_hdr.msgh_remote_port = p;
  err = mach_msg ((mach_msg_header_t *)msg,
                  MACH_SEND_MSG,
                  msg->msg_hdr.msgh_size, 0, MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  return err;
}

static io_return_t
netif_rx_handle (uint8_t *data, int len, mach_port_t dest)
{
  int pack_size;
  struct net_rcv_msg net_msg;
  struct ether_header *eh;
  struct packet_header *ph;
  size_t align = sizeof (uintptr_t);

  pack_size = len - sizeof (struct ethhdr);
  /* remember message sizes must be rounded up */
  net_msg.msg_hdr.msgh_size =
    (((mach_msg_size_t) (offsetof (struct net_rcv_msg, packet)
                         + sizeof (struct packet_header)
                         + pack_size)) + align-1) & ~(align-1);

  /* Copy packet into message buffer.  */
  eh = (struct ether_header *) (net_msg.header);
  ph = (struct packet_header *) (net_msg.packet);
  memcpy (eh, data, sizeof (struct ether_header));
  /* packet is prefixed with a struct packet_header,
     see include/device/net_status.h.  */
  memcpy (ph + 1, data + sizeof (struct ether_header), pack_size);
  ph->type = eh->ether_type;
  ph->length = pack_size + sizeof (struct packet_header);

  net_msg.sent = FALSE; /* Mark packet as received.  */

  net_msg.header_type = header_type;
  net_msg.packet_type = packet_type;
  net_msg.net_rcv_msg_packet_count = ph->length;
  return deliver_msg (&net_msg, dest);
}

static io_return_t
rumpnet_device_open (mach_port_t reply_port,
		     mach_msg_type_name_t reply_port_type,
		     dev_mode_t mode, const char *name, device_t *devp,
		     mach_msg_type_name_t *devicePoly)
{
  io_return_t err = D_SUCCESS;
  struct ifreq *dev;
  struct net_data *nd;

  /* Search for the device.  */
  dev = search_interface (name);
  if (!dev)
    {
      fprintf (stderr, "after search_interface: cannot find %s\n", name);
      return D_NO_SUCH_DEVICE;
    }

  /* Allocate and initialize device data if this is the first open.  */
  nd = search_nd (dev);
  if (!nd)
    {
      err = machdev_create_device_port (sizeof (*nd), &nd);
      if (err)
	{
	  fprintf (stderr, "after machdev_create_device_port: cannot create a port\n");
	  goto out;
	}

      nd->dev = dev;
      nd->device.emul_data = nd;
      nd->device.emul_ops = &rump_net_emulation_ops;
      nd->next = nd_head;
      nd_head = nd;

      if ((err = init_interface(nd) < 0))
        {
          mach_print ("after init_interface: cannot init the device\n");
          goto out;
	}
      if ((err = up_interface(dev) < 0))
        {
          mach_print ("after up_interface: cannot bring up the device\n");
	  goto out;
	}

out:
      if (err)
	{
	  if (nd)
	    {
	      ports_destroy_right (nd);
	      nd = NULL;
	    }
	}
    }

  if (nd)
    {
      *devp = ports_get_right (nd);
      *devicePoly = MACH_MSG_TYPE_MAKE_SEND;
    }
  return err;
}

static io_return_t
send_packet (struct net_data *nd, io_buf_ptr_t buf, unsigned int bytes)
{
  io_return_t err;
  struct iovec iov;
  int result;

  iov.iov_base = buf;
  iov.iov_len = bytes;

  result = rump_sys_writev (nd->bpf_fd, &iov, 1);
  if (result < 0)
    {
      errno = EIO;
      mach_print("ERROR: rump_sys_writev(bpf)\n");
      return -1;
    }

  return result;
}

static io_return_t
receive_packets (struct net_data *nd)
{
  io_return_t err;
  int i;
  int fragment;
  int pkt_length;
  int read_length;
  int todo;
  int buf_size = BPF_BUFSIZE;
  struct pollfd pfd;
  size_t buf_inc;
  struct bpf_hdr *bp = NULL;
  struct ethhdr *hdr = NULL;
  bool own_traffic = true;
  rpc_phys_addr_t pap;
  /* reusable packet buffer */
  static vm_address_t bpf_pkt_addr = 0;
  static uint8_t *bpf_pkt = NULL;

  pfd.fd = nd->bpf_fd;
  pfd.events = RUMP_POLLIN;

  if (!bpf_pkt_addr)
    {
      err = vm_allocate (mach_task_self (), &bpf_pkt_addr, buf_size, TRUE);
      if (err != KERN_SUCCESS)
        {
          mach_print("ERROR: cannot vm_allocate\n");
          errno = ENOMEM;
          return -1;
        }
      bpf_pkt = (uint8_t *)bpf_pkt_addr;

        {
          volatile uint8_t dummy_read __attribute__ ((unused));
          int npages = (buf_size + PAGE_SIZE - 1) / PAGE_SIZE;
          int i;

          /* Fault-in the memory pages by reading a single byte of each */
          for (i = 0; i < npages; i++)
            dummy_read = ((volatile uint8_t *)bpf_pkt)[i * PAGE_SIZE];
        }
    }

poll_again:
  switch (rump_sys_poll(&pfd, 1, NET_RCV_WAIT))
    {
      case 0:
      case -1:
        goto poll_again;
      default:
        break;
    }

  read_length = rump_sys_read (nd->bpf_fd, bpf_pkt, buf_size);
  if (read_length >= 0 && read_length < SIZEOF_BPF_HDR)
    goto poll_again;
  else if (read_length < 0)
    {
      switch (rump_errno2host(errno))
        {
          case EAGAIN:
          case EINTR:
            goto poll_again;

          case ENXIO:
          case EIO:
          default:
            {
              mach_print("ERROR: rump_sys_read(bpf)\n");
              vm_deallocate (mach_task_self (), bpf_pkt_addr, buf_size);
              bpf_pkt_addr = 0;
              return -2; /* device gone */
            }
        }
    }

  todo = read_length;

  while (own_traffic || (todo > 0))
    {
      bp = (struct bpf_hdr *)bpf_pkt;
      hdr = (struct ethhdr *)(bpf_pkt + bp->bh_hdrlen);
      fragment = bp->bh_datalen - bp->bh_caplen;
      pkt_length = bp->bh_caplen;
      buf_inc = BPF_WORDALIGN(pkt_length + bp->bh_hdrlen);
      todo -= buf_inc;

      if (fragment)
        return 0;

      if (!cmp_hwaddr(hdr->h_source, nd->hw_address))
	{
          own_traffic = true;
          mach_print("seeing our own traffic\n");
	}
      else
        {
	  /* rcv this packet */
	  own_traffic = false;
          err = netif_rx_handle((uint8_t *)hdr, pkt_length, nd->dest);
          /* Ignore errors due to:
	   * not enough bandwidth in software stack to handle all packets
	   */
        }

      /* Check for last packet in bpf buffer */
      if (todo > 0)
        bpf_pkt += buf_inc;
      else
        bpf_pkt = (uint8_t *)bpf_pkt_addr;
    }
  return 0;
}

static void *
rcv_process(void *arg)
{
  io_return_t err;
  struct net_data *nd = (struct net_data *)arg;
  int length;

  for (;;)
    {
      err = receive_packets (nd);
      if (err == -1)
        {
          mach_print("ERROR: cannot rcv any packets, giving up\n");
          return NULL;
        }
      else if (err == -2)
        {
          mach_print("ERROR: device gone, retry\n");
          sleep(2);
	}
    }
}

static io_return_t
rumpnet_device_write (void *d, mach_port_t reply_port,
		      mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		      recnum_t bn, io_buf_ptr_t data, unsigned int count,
		      int *bytes_written)
{
  struct net_data *nd = (struct net_data *)d;
  error_t err;

  err = send_packet(nd, data, count);
  if (err < 0)
    return errno;
  *bytes_written = err;

  if (*bytes_written != count)
    {
      mach_print("ERROR: bytes_written != count\n");
      return D_IO_ERROR;
    }

  vm_deallocate (mach_task_self (), (vm_address_t) data, count);

  return D_SUCCESS;
}

static io_return_t
device_get_status (void *d, dev_flavor_t flavor, dev_status_t status,
		   mach_msg_type_number_t *count)
{
  struct net_data *nd = (struct net_data *)d;
  io_return_t err;

  switch (flavor)
    {
      case NET_FLAGS:
        {
	  if (*count != 1)
            return D_INVALID_SIZE;

          err = rump_sys_ioctl(socket_aflink, SIOCGIFFLAGS, nd->dev);
          if (err < 0)
            return D_IO_ERROR;

          *(int *) status = nd->dev->ifr_flags;
        }
      break;

      case NET_STATUS:
        {
	  struct net_status *ns = (struct net_status *)status;

          if (*count < NET_STATUS_COUNT)
            return D_INVALID_OPERATION;

          ns->min_packet_size = ETH_HLEN;
          ns->max_packet_size = SIZEOF_ETH_FRAME;
          ns->header_format   = HDR_ETHERNET;
          ns->header_size     = ETH_HLEN;
          ns->address_size    = ETH_ALEN;
          ns->flags           = nd->dev->ifr_flags;
          ns->mapped_size     = 0;

          *count = NET_STATUS_COUNT;
        }
      break;

      case NET_ADDRESS:
        {
          err = rump_sys_ioctl(socket_aflink, SIOCGIFFLAGS, nd->dev);
          if (err < 0)
            return D_IO_ERROR;

          err = get_hwaddr(nd->dev->ifr_name, nd->hw_address);
          if (err)
            return D_IO_ERROR;

          status[0] =
             ((nd->hw_address[0] << 24) |
              (nd->hw_address[1] << 16) |
              (nd->hw_address[2] << 8) |
              (nd->hw_address[3]));

          status[1] =
             ((nd->hw_address[4] << 24) |
              (nd->hw_address[5] << 16));

          *count = 2;
        }
      break;

      default:
        return D_INVALID_OPERATION;
    }
  return D_SUCCESS;
}

static io_return_t
device_set_status(void *d, dev_flavor_t flavor, dev_status_t status,
		  mach_msg_type_number_t count)
{
  io_return_t err;
  struct net_data *nd = (struct net_data *)d;

  if (flavor != NET_FLAGS)
    {
      mach_print("Some other flavor\n");
      return D_INVALID_OPERATION;
    }

  if (count != 1)
    return D_INVALID_SIZE;

  err = rump_sys_ioctl(socket_aflink, SIOCGIFFLAGS, nd->dev);
  if (err < 0)
    return D_IO_ERROR;

  nd->dev->ifr_flags = *((int *)status);

  err = rump_sys_ioctl(socket_aflink, SIOCSIFFLAGS, nd->dev);
  if (err < 0)
    return D_IO_ERROR;

  return D_SUCCESS;
}

static io_return_t
device_set_filter (void *d, mach_port_t port, int priority,
		   filter_t * filter, unsigned filter_count)
{
//  return net_set_filter (&((struct net_data *) d)->ifnet.port_list,
//			 port, priority, filter, filter_count);
  struct net_data *nd = (struct net_data *)d;
  nd->dest = port;

  return D_SUCCESS;
}

static void rumpnet_init (void)
{
  rump_init();
  socket_init();
}

static struct machdev_device_emulation_ops rump_net_emulation_ops =
{
  rumpnet_init,
  NULL,
  NULL,
  dev_to_port,
  rumpnet_device_open,
  NULL,
  rumpnet_device_write,
  NULL,
  NULL,
  NULL,
  device_set_status,
  device_get_status,
  device_set_filter,
  NULL,
  NULL,
  NULL,
  NULL
};

void rump_register_net(void)
{
  machdev_register (&rump_net_emulation_ops);
}
