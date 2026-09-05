/* Load a task using the single server, and then run it
   as if we were the kernel.
   Copyright (C) 1993,94,95,96,97,98,99,2000,01,02,2006,14,16
     Free Software Foundation, Inc.
   Copyright (C) 2026 Alperen ERKAN

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <mach/notify.h>
#include <device/device.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/task_notify.h>
#include <mach/gnumach.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <mach/mig_support.h>
#include <argp.h>
#include <hurd/store.h>
#include <hurd/ihash.h>
#include <sys/reboot.h>
#include <sys/mman.h>
#include <version.h>

#include "default_pager_U.h"
#include "notify_S.h"
#include "device_S.h"
#include "io_S.h"
#include "device_reply_U.h"
#include "io_reply_U.h"
#include "term_S.h"
/* #include "tioctl_S.h" */
#include "mach_S.h"
#include "mach_host_S.h"
#include "gnumach_S.h"
#include "task_notify_S.h"

#include "boot_script.h"

#include <hurd/auth.h>

#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <error.h>
#include <hurd.h>
#include <assert-backtrace.h>

#include "private.h"

/* We support two modes of operation.  Traditionally, Subhurds were
   privileged, i.e. they had the privileged kernel ports.  This has a
   few drawbacks.  Privileged subhurds can manipulate all tasks on the
   system and halt the system.  Nowadays we allow an unprivileged
   mode.  */
static int privileged;
static int want_privileged;

static struct termios orig_tty_state;
static int termstate_initialized;
static int isig;
static char *kernel_command_line;

static void
restore_termstate (void)
{
  if (! termstate_initialized)
    return;
  tcsetattr (0, 0, &orig_tty_state);
  termstate_initialized = 0;
}

static void
sig_handler (int sig)
{
  switch (sig)
    {
    case SIGCONT:
      /* Re-enter raw mode after being stopped.  */
      if (termstate_initialized)
	{
	  struct termios tty_state = orig_tty_state;
	  cfmakeraw (&tty_state);
	  if (isig)
	    tty_state.c_lflag |= ISIG;
	  tcsetattr (0, 0, &tty_state);
	}
      break;

    case SIGTSTP:
      restore_termstate ();
      signal (SIGTSTP, SIG_DFL);
      raise (SIGTSTP);
      break;

    default:
      restore_termstate ();
      signal (sig, SIG_DFL);
      raise (sig);
      break;
    }
}

static void
init_termstate (void)
{
  struct termios tty_state;

  if (tcgetattr (0, &tty_state) < 0)
    error (10, errno, "tcgetattr");

  orig_tty_state = tty_state;
  cfmakeraw (&tty_state);
  if (isig)
    tty_state.c_lflag |= ISIG;

  if (tcsetattr (0, 0, &tty_state) < 0)
    error (11, errno, "tcsetattr");

  termstate_initialized = 1;

  atexit (restore_termstate);
  signal (SIGINT, sig_handler);
  signal (SIGTERM, sig_handler);
  signal (SIGTSTP, sig_handler);
  signal (SIGCONT, sig_handler);
}

#define host_fstat fstat
typedef struct stat host_stat_t;

void __attribute__ ((__noreturn__))
host_exit (int status)
{
  restore_termstate ();
  exit (status);
}

/* Best-effort write of a diagnostic message to stderr.  */
static void
write_diag (const char *msg, size_t len)
{
  ssize_t err;
  do
    err = write (2, msg, len);
  while (err < 0 && errno == EINTR);
}

int verbose;

mach_port_t privileged_host_port, master_device_port;
mach_port_t pseudo_privileged_host_port;
mach_port_t pseudo_master_device_port;
mach_port_t receive_set;
mach_port_t pseudo_console, pseudo_root, pseudo_time;
mach_port_t pseudo_pset;
task_t pseudo_kernel;
mach_port_t task_notification_port;
mach_port_t dead_task_notification_port;
auth_t authserver;

/* The proc server registers for new task notifications which we will
   send to this port.  */
mach_port_t new_task_notification;

struct store *root_store;

pthread_spinlock_t queuelock = PTHREAD_SPINLOCK_INITIALIZER;
pthread_spinlock_t readlock = PTHREAD_SPINLOCK_INITIALIZER;

mach_port_mscount_t console_mscount;

char bootstrap_args[100] = "-";
char *bootdevice = 0;
char *bootscript = 0;


extern char *useropen_dir;

/* XXX: glibc should provide mig_reply_setup but does not.  */
/* Fill in default response.  */
void
mig_reply_setup (
	const mach_msg_header_t	*in,
	mach_msg_header_t	*out)
{
      static const mach_msg_type_t RetCodeType = {
        .msgt_name = MACH_MSG_TYPE_INTEGER_32,
        .msgt_size = 32,
        .msgt_number = 1,
        .msgt_inline = TRUE,
        .msgt_longform = FALSE,
        .msgt_deallocate = FALSE,
        .msgt_unused = 0
      };

#define	InP	(in)
#define	OutP	((mig_reply_header_t *) out)
      OutP->Head.msgh_bits =
	MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(InP->msgh_bits), 0);
      OutP->Head.msgh_size = sizeof *OutP;
      OutP->Head.msgh_remote_port = InP->msgh_remote_port;
      OutP->Head.msgh_local_port = MACH_PORT_NULL;
      OutP->Head.msgh_seqno = 0;
      OutP->Head.msgh_id = InP->msgh_id + 100;
      OutP->RetCodeType = RetCodeType;
      OutP->RetCode = MIG_BAD_ID;
#undef InP
#undef OutP
}

error_t
mach_msg_forward (mach_msg_header_t *inp,
                  mach_port_t destination, mach_msg_type_name_t destination_type)
{
  /* Put the reply port back at the correct position, insert new
     destination.  */
  inp->msgh_local_port = inp->msgh_remote_port;
  inp->msgh_remote_port = destination;
  inp->msgh_bits =
    MACH_MSGH_BITS (destination_type, MACH_MSGH_BITS_REMOTE (inp->msgh_bits))
    | MACH_MSGH_BITS_OTHER (inp->msgh_bits);

  /* A word about resources carried in complex messages.

     "In a received message, msgt_deallocate is TRUE in type
     descriptors for out-of-line memory".  Therefore, "[the
     out-of-line memory] is implicitly deallocated from the sender
     [when we resend the message], as if by vm_deallocate".

     Similarly, rights in messages will be either
     MACH_MSG_TYPE_PORT_SEND, MACH_MSG_TYPE_PORT_SEND_ONCE, or
     MACH_MSG_TYPE_PORT_RECEIVE.  These types are aliases for,
     respectively, MACH_MSG_TYPE_MOVE_SEND,
     MACH_MSG_TYPE_MOVE_SEND_ONCE, and MACH_MSG_TYPE_MOVE_RECEIVE.
     Therefore, the rights are moved when we resend the message.  */

  return mach_msg (inp, MACH_SEND_MSG, inp->msgh_size,
                   0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

int
boot_demuxer (mach_msg_header_t *inp,
	      mach_msg_header_t *outp)
{
  error_t err;
  mig_routine_t routine;
  mig_reply_setup (inp, outp);

  if (inp->msgh_local_port == task_notification_port
      && MACH_PORT_VALID (new_task_notification)
      && 24000 <= inp->msgh_id && inp->msgh_id < 24100)
    {
      /* This is a message of the Process subsystem.  We relay this to
         allow the "outer" proc servers to communicate with the "inner"
         one.  */
      mig_reply_header_t *reply = (mig_reply_header_t *) outp;

      if (MACH_PORT_VALID (new_task_notification))
        err = mach_msg_forward (inp, new_task_notification, MACH_MSG_TYPE_COPY_SEND);
      else
        err = EOPNOTSUPP;

      if (err)
        reply->RetCode = err;
      else
        reply->RetCode = MIG_NO_REPLY;

      return TRUE;
    }

  if ((routine = io_server_routine (inp)) ||
      (routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)) ||
      (routine = term_server_routine (inp)) ||
      (routine = mach_server_routine (inp)) ||
      (routine = mach_host_server_routine (inp)) ||
      (routine = gnumach_server_routine (inp)) ||
      (routine = task_notify_server_routine (inp))
      /* (routine = tioctl_server_routine (inp)) */)
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

static void read_reply (void);
static void * msg_thread (void *);
static void * select_thread (void *);

const char *argp_program_version = STANDARD_HURD_VERSION (boot);

#define OPT_PRIVILEGED	-1
#define OPT_BOOT_SCRIPT	-2

static struct argp_option options[] =
{
  { NULL, 0, NULL, 0, "Boot options:" },
  { "boot-script", OPT_BOOT_SCRIPT, "BOOT-SCRIPT", 0,
    "boot script to execute" },
  { "boot-root",   'D', "DIR", 0,
    "Root of a directory tree in which to find files specified in BOOT-SCRIPT" },
  { "single-user", 's', 0, 0,
    "Boot in single user mode" },
  { "kernel-command-line", 'c', "COMMAND LINE", 0,
    "Simulated multiboot command line to supply" },
  { "verbose",     'v', 0, 0,
    "Be verbose" },
  { "pause" ,      'd', 0, 0,
    "Pause for user confirmation at various times during booting" },
  { "isig",      'I', 0, 0,
    "Do not disable terminal signals, so you can suspend and interrupt boot"},
  { "device",	   'f', "SUBHURD_NAME=DEVICE_FILE", 0,
    "Pass the given DEVICE_FILE to the Subhurd as device SUBHURD_NAME"},
  { "privileged", OPT_PRIVILEGED, NULL, 0,
    "Allow the subhurd to access privileged kernel ports"},
  { 0 }
};
static char doc[] = "Boot a second hurd";



/* Device pass through.  */

struct dev_map
{
  char *device_name;	/* The name of the device in the Subhurd.  */
  char *file_name;	/* The filename outside the Subhurd.  */
  struct dev_map *next;
};

static struct dev_map *dev_map_head;

static struct dev_map *
add_dev_map (const char *dev_name, const char *dev_file)
{
  file_t node;
  struct dev_map *map;

  /* See if we can open the file.  */
  node = file_name_lookup (dev_file, 0, 0);
  if (! MACH_PORT_VALID (node))
    error (1, errno, "%s", dev_file);
  mach_port_deallocate (mach_task_self (), node);

  map = malloc (sizeof *map);
  if (map == NULL)
    return NULL;

  map->device_name = strdup (dev_name);
  map->file_name = strdup (dev_file);
  if (! map->device_name || ! map->file_name)
    {
      free (map->device_name);
      free (map->file_name);
      free (map);
      return NULL;
    }
  map->next = dev_map_head;
  dev_map_head = map;
  return map;
}

static struct dev_map *lookup_dev (const char *dev_name)
{
  struct dev_map *map;

  for (map = dev_map_head; map; map = map->next)
    {
      if (strcmp (map->device_name, dev_name) == 0)
	return map;
    }
  return NULL;
}

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  char *dev_file;

  switch (key)
    {
      size_t len;

    case 'c':  kernel_command_line = arg; break;

    case 'D':  useropen_dir = arg; break;

    case 'I':  isig = 1; break;

    case 'v':
      verbose += 1;
      break;

    case 's': case 'd':
      len = strlen (bootstrap_args);
      if (len >= sizeof bootstrap_args - 1)
	argp_error (state, "Too many bootstrap args");
      bootstrap_args[len++] = key;
      bootstrap_args[len] = '\0';
      break;

    case 'f':
      dev_file = strchr (arg, '=');
      if (dev_file == NULL)
	return ARGP_ERR_UNKNOWN;
      *dev_file = 0;
      if (! add_dev_map (arg, dev_file + 1))
	argp_error (state, "Not enough memory");
      break;

    case OPT_PRIVILEGED:
      want_privileged = 1;
      break;

    case OPT_BOOT_SCRIPT:
      bootscript = arg;
      break;

    case ARGP_KEY_ARG:
      return ARGP_ERR_UNKNOWN;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
allocate_pseudo_ports (void)
{
  error_t err;
  mach_port_t old;

  /* Allocate a port that we hand out as the privileged host port.  */
  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pseudo_privileged_host_port);
  if (err)
    return err;
  err = mach_port_insert_right (mach_task_self (),
				pseudo_privileged_host_port,
				pseudo_privileged_host_port,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    return err;
  err = mach_port_move_member (mach_task_self (), pseudo_privileged_host_port,
			       receive_set);
  if (err)
    return err;
  err = mach_port_request_notification (mach_task_self (),
                                        pseudo_privileged_host_port,
					MACH_NOTIFY_NO_SENDERS, 1,
					pseudo_privileged_host_port,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
  if (err)
    return err;
  assert_backtrace (old == MACH_PORT_NULL);

  /* Allocate a port that we hand out as the privileged processor set
     port.  */
  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pseudo_pset);
  if (err)
    return err;
  err = mach_port_move_member (mach_task_self (), pseudo_pset,
			       receive_set);
  if (err)
    return err;
  /* Make one send right that we copy when handing it out.  */
  err = mach_port_insert_right (mach_task_self (),
				pseudo_pset,
				pseudo_pset,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    return err;

  /* We will receive new task notifications on this port.  */
  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &task_notification_port);
  if (err)
    return err;
  err = mach_port_move_member (mach_task_self (), task_notification_port,
			       receive_set);
  if (err)
    return err;

  /* And information about dying tasks here.  */
  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &dead_task_notification_port);
  if (err)
    return err;
  err = mach_port_move_member (mach_task_self (), dead_task_notification_port,
			       receive_set);
  if (err)
    return err;

  return 0;
}

void
read_boot_script (char **buffer, size_t *length)
{
  char *p, *buf;
  static const char filemsg[] = "Can't open boot script\n";
  static const char memmsg[] = "Not enough memory\n";
  int i, fd;
  size_t amt, len;

  fd = open (bootscript, O_RDONLY, 0);
  if (fd < 0)
    {
      write_diag (filemsg, sizeof filemsg - 1);
      host_exit (1);
    }
  p = buf = malloc (500);
  if (!buf)
    {
      write_diag (memmsg, sizeof memmsg - 1);
      host_exit (1);
    }
  len = 500;
  amt = 0;
  while (1)
    {
      i = read (fd, p, len - (p - buf));
      if (i == 0)
        break;
      if (i < 0)
        {
          if (errno == EINTR)
            continue;
          error (1, errno, "%s", bootscript);
        }
      p += i;
      amt += i;
      if (p == buf + len)
        {
          char *newbuf;
          size_t newlen = len * 2;

          if (newlen < len)
            error (1, ENOMEM, "%s", bootscript);
          newbuf = realloc (buf, newlen);
          if (!newbuf)
            {
              write_diag (memmsg, sizeof memmsg - 1);
              host_exit (1);
            }
          p = newbuf + len;
          len = newlen;
          buf = newbuf;
        }
    }

  close (fd);
  *buffer = buf;
  *length = amt;
}


/* Boot script file for booting contemporary GNU Hurd systems.  Each
   line specifies a file to be loaded by the boot loader (the first
   word), and actions to be done with it.  */
const char *default_boot_script =
  /* First, the bootstrap filesystem.  It needs several ports as
     arguments, as well as the user flags from the boot loader.  */
  "/hurd/ext2fs.static"
  " --readonly"
  " --multiboot-command-line=${kernel-command-line}"
  " --host-priv-port=${host-port}"
  " --device-master-port=${device-port}"
  " --kernel-task=${kernel-task}"
  " --exec-server-task=${exec-task}"
  " -T device ${root-device} $(task-create) $(task-resume)"
  "\n"

  /* Now the exec server.  It is created suspended; the bootstrap
     filesystem resumes it once it is ready.  Its task port is saved
     in ${exec-task} to be passed to the fs above.  */
  "/hurd/exec.static $(exec-task=task-create)"
  "\n";


int
main (int argc, char **argv, char **envp)
{
  error_t err;
  mach_port_t foo;
  char *buf = 0;
  pthread_t pthread_id;
  char *root_store_name;
  const struct argp_child kids[] = { { &store_argp, 0, "Store options:", -2 },
                                     { 0 }};
  struct argp argp = { options, parse_opt, NULL, doc, kids };
  struct store_argp_params store_argp_params = { 0 };

  argp_parse (&argp, argc, argv, 0, 0, &store_argp_params);
  err = store_parsed_name (store_argp_params.result, &root_store_name);
  if (err)
    error (2, err, "store_parsed_name");

  err = store_parsed_open (store_argp_params.result, 0, &root_store);
  if (err)
    error (4, err, "%s", root_store_name);

  if (want_privileged)
    {
      if (get_privileged_ports (&privileged_host_port, &master_device_port))
        error (1, 0, "Must be run as root for privileged subhurds");

      privileged = MACH_PORT_VALID (master_device_port);
      if (! privileged)
        error (1, 0, "Must be run as root for privileged subhurds");
    }

  if (privileged)
    strcat (bootstrap_args, "f");

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
			    &receive_set);
  if (err)
    error (12, err, "mach_port_allocate");

  if (pipe2 (wake_pipe, O_NONBLOCK | O_CLOEXEC) < 0
      || pipe2 (select_pipe, O_NONBLOCK | O_CLOEXEC) < 0)
    error (13, errno, "pipe2");

  if (root_store->class == &store_device_class && root_store->name
      && (root_store->flags & STORE_ENFORCED)
      && root_store->num_runs == 1
      && root_store->runs[0].start == 0
      && privileged)
    /* Let known device nodes pass through directly.  */
    bootdevice = root_store->name;
  else
    /* Pass a magic value that we can use to do I/O to ROOT_STORE.  */
    {
      bootdevice = "pseudo-root";
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&pseudo_root);
      if (err)
	error (14, err, "mach_port_allocate");
      err = mach_port_move_member (mach_task_self (), pseudo_root, receive_set);
      if (err)
	error (14, err, "mach_port_move_member");
    }

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pseudo_master_device_port);
  if (err)
    error (15, err, "mach_port_allocate");
  err = mach_port_insert_right (mach_task_self (),
				pseudo_master_device_port,
				pseudo_master_device_port,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    error (15, err, "mach_port_insert_right");
  err = mach_port_move_member (mach_task_self (), pseudo_master_device_port,
			       receive_set);
  if (err)
    error (15, err, "mach_port_move_member");
  err = mach_port_request_notification (mach_task_self (),
					pseudo_master_device_port,
					MACH_NOTIFY_NO_SENDERS, 1,
					pseudo_master_device_port,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (err)
    error (15, err, "mach_port_request_notification");
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pseudo_console);
  if (err)
    error (16, err, "mach_port_allocate");
  err = mach_port_move_member (mach_task_self (), pseudo_console, receive_set);
  if (err)
    error (16, err, "mach_port_move_member");
  err = mach_port_request_notification (mach_task_self (), pseudo_console,
					MACH_NOTIFY_NO_SENDERS, 1, pseudo_console,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (err)
    error (16, err, "mach_port_request_notification");
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pseudo_time);
  if (err)
    error (17, err, "mach_port_allocate");
  err = mach_port_move_member (mach_task_self (), pseudo_time, receive_set);
  if (err)
    error (17, err, "mach_port_move_member");
  err = mach_port_request_notification (mach_task_self (), pseudo_time,
					MACH_NOTIFY_NO_SENDERS, 1, pseudo_time,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (err)
    error (17, err, "mach_port_request_notification");
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  if (! privileged)
    {
      err = allocate_pseudo_ports ();
      if (err)
        error (1, err, "Allocating pseudo ports");

      /* Create a new task namespace for us.  */
      err = proc_make_task_namespace (getproc (), task_notification_port,
                                      MACH_MSG_TYPE_MAKE_SEND);
      if (err)
        error (1, err, "proc_make_task_namespace");

      /* Create an empty task that the subhurds can freely frobnicate.  */
      err = task_create (mach_task_self (), 0, &pseudo_kernel);
      if (err)
        error (1, err, "task_create");

      /* Give it a name so it's easy to spot it from the real kernel.  */
      err = task_set_name (pseudo_kernel, "pseudo_kernel");
      if (err)
	error (1, err, "task_set_name");
    }

  if (kernel_command_line == 0)
    {
      int err2 = asprintf (&kernel_command_line, "%s %s root=%s",
		argv[0], bootstrap_args, bootdevice);
      assert_backtrace (err2 != -1);
    }

  /* Initialize boot script variables.  */
  if (boot_script_set_variable ("host-port", VAL_PORT,
                                privileged
                                ? privileged_host_port
				: pseudo_privileged_host_port)
      || boot_script_set_variable ("device-port", VAL_PORT,
				   pseudo_master_device_port)
      || boot_script_set_variable ("kernel-task", VAL_PORT,
				   pseudo_kernel)
      || boot_script_set_variable ("kernel-command-line", VAL_STR,
				   (intptr_t) kernel_command_line)
      || boot_script_set_variable ("root-device",
				   VAL_STR, (intptr_t) bootdevice)
      || boot_script_set_variable ("boot-args",
				   VAL_STR, (intptr_t) bootstrap_args))
    {
      static const char msg[] = "error setting variable";
      write_diag (msg, sizeof msg - 1);
      host_exit (1);
    }

  /* Turn each `FOO=BAR' word in the command line into a boot script
     variable ${FOO} with value BAR.  */
  {
    char *s = strdup (kernel_command_line);
    char *word;

    if (! s)
      error (1, ENOMEM, "strdup");

    while ((word = strsep (&s, " \t")) != 0)
      {
       char *eq = strchr (word, '=');
       if (eq == 0)
         continue;
       *eq++ = '\0';
       if (! strcmp (word, "host-port")
           || ! strcmp (word, "device-port")
           || ! strcmp (word, "kernel-task")
           || ! strcmp (word, "kernel-command-line")
           || ! strcmp (word, "root-device")
           || ! strcmp (word, "boot-args"))
         {
           fprintf (stderr, "ignoring reserved boot variable %s\n", word);
           continue;
         }
       err = boot_script_set_variable (word, VAL_STR, (intptr_t) eq);
       if (err)
         {
           char *msg;
           if (asprintf (&msg, "cannot set boot-script variable %s: %s\n",
                         word, boot_script_error_string (err)) >= 0)
             {
               write_diag (msg, strlen (msg));
               free (msg);
             }
           host_exit (1);
         }
      }
    free (s);
  }

  /* Parse the boot script.  */
  {
    char *p, *line;
    size_t amt;
    int lineno = 1;

    if (bootscript)
      read_boot_script (&buf, &amt);
    else
      {
	buf = strdup (default_boot_script);
	if (! buf)
	  error (1, ENOMEM, "strdup");
	amt = strlen (default_boot_script);
      }

    line = p = buf;
    while (1)
      {
	while (p < buf + amt && *p != '\n')
	  p++;
	*p = '\0';
	err = boot_script_parse_line (0, line);
	if (err)
	  {
	    char *str;

	    str = boot_script_error_string (err);
	    fprintf (stderr, "line %d: ", lineno);
	    write_diag (str, strlen (str));
	    write_diag (" in `", 5);
	    write_diag (line, strlen (line));
	    write_diag ("'\n", 2);
	    host_exit (1);
	  }
	if (p == buf + amt)
	  break;
	line = ++p;
	lineno++;
      }
  }

  if (index (bootstrap_args, 'd'))
    {
      static const char msg[] = "Pausing. . .";
      char c;
      ssize_t r;

      write_diag (msg, sizeof msg - 1);
      do
	r = read (0, &c, 1);
      while (r < 0 && errno == EINTR);
    }

  init_termstate ();

  /* The boot script has now been parsed into internal data structures.
     Now execute its directives.  */
  {
    err = boot_script_exec ();
    if (err)
      {
	char *str = boot_script_error_string (err);

	write_diag (str, strlen (str));
	write_diag ("\n",  1);
	host_exit (1);
      }
    free (buf);
  }

  mach_port_deallocate (mach_task_self (), pseudo_master_device_port);

  err = pthread_create (&pthread_id, NULL, msg_thread, NULL);
  if (err)
    error (1, err, "pthread_create");
  pthread_detach (pthread_id);

  err = pthread_create (&pthread_id, NULL, select_thread, NULL);
  if (err)
    error (1, err, "pthread_create");
  pthread_detach (pthread_id);

  for (;;)
    {
      int want_stdin;
      struct pollfd pfd[2];
      int n;

      if (atomic_load_explicit (&stdin_eof, memory_order_relaxed))
	{
	  /* Satisfy remaining waiters with EOF replies.  */
	  pthread_spin_lock (&queuelock);
	  want_stdin = qrhead != NULL;
	  pthread_spin_unlock (&queuelock);
	  if (want_stdin)
	    {
	      read_reply ();
	      continue;
	    }
	}
      else
	{
	  pthread_spin_lock (&queuelock);
	  want_stdin = qrhead != NULL;
	  pthread_spin_unlock (&queuelock);
	}

      pfd[0].fd = wake_pipe[0];
      pfd[0].events = POLLIN;
      pfd[0].revents = 0;
      pfd[1].fd = 0;
      pfd[1].events = POLLIN;
      pfd[1].revents = 0;

      n = poll (pfd, want_stdin ? 2 : 1, -1);
      if (n < 0)
	{
	  if (errno == EINTR)
	    continue;
	  error (5, errno, "poll");
	}

      if (pfd[0].revents & POLLIN)
	{
	  char c[128];
	  read (wake_pipe[0], c, sizeof c);
	}

      if (want_stdin && (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)))
	read_reply ();
    }
}

void * __attribute__ ((noreturn))
msg_thread (void *arg)
{
  pthread_setname_np (pthread_self (), "msg");
  while (1)
    mach_msg_server (boot_demuxer, 0, receive_set);
}


enum read_type
{
  DEV_READ,
  DEV_READI,
  IO_READ,
};
struct qr
{
  enum read_type type;
  mach_port_t reply_port;
  mach_msg_type_name_t reply_type;
  vm_size_t amount;
  struct qr *next;
};
struct qr *qrhead, *qrtail;

/* Console input event handling.  The main thread polls the host stdin
   only while console read requests are queued; the message threads
   wake it via WAKE_PIPE.  */
static int wake_pipe[2];
static int select_pipe[2];
static _Atomic int stdin_eof;

/* Maximum size of a single console read request (out-of-line).  */
#define CONSOLE_READ_MAX (16 * 1024 * 1024)

struct selq
{
  mach_port_t reply_port;
  mach_msg_type_name_t reply_type;
  int is_timeout;		/* Use io_select_timeout_reply.  */
  int type;			/* Requested SELECT_* mask.  */
  struct timespec deadline;	/* Valid iff IS_TIMEOUT.  */
  struct selq *next;
};
static struct selq *selq_head, *selq_tail;
static pthread_mutex_t selq_lock = PTHREAD_MUTEX_INITIALIZER;

/* Send the reply for a queued console read QR.  BUF/LEN are the data;
   if ERR is nonzero, it is an errno-style error code and no data is
   returned.  */
static void
send_read_reply (struct qr *qr, const void *buf, ssize_t len, int err)
{
  switch (qr->type)
    {
    case DEV_READ:
      ds_device_read_reply (qr->reply_port, qr->reply_type, err,
			    (io_buf_ptr_t) (err ? 0 : buf),
			    err ? 0 : len);
      break;

    case DEV_READI:
      ds_device_read_reply_inband (qr->reply_port, qr->reply_type, err,
				   err ? (const void *) 0 : buf,
				   err ? 0 : len);
      break;

    case IO_READ:
      io_read_reply (qr->reply_port, qr->reply_type, err,
		     err ? (void *) 0 : buf, err ? 0 : len);
      break;
    }
}

/* Queue a read for later reply.  */
static kern_return_t
queue_read (enum read_type type, mach_port_t reply_port,
	    mach_msg_type_name_t reply_type, vm_size_t amount)
{
  struct qr *qr;

  /* Zero-length requests and EOF get an immediate answer.  */
  if (amount == 0 || atomic_load_explicit (&stdin_eof, memory_order_relaxed))
    {
      struct qr qr0 = { type, reply_port, reply_type, 0, NULL };
      send_read_reply (&qr0, NULL, 0, 0);
      return D_SUCCESS;
    }

  qr = malloc (sizeof *qr);
  if (!qr)
    return D_NO_MEMORY;

  qr->type = type;
  qr->reply_port = reply_port;
  qr->reply_type = reply_type;
  qr->amount = amount;
  qr->next = 0;

  pthread_spin_lock (&queuelock);
  if (qrtail)
    qrtail->next = qr;
  else
    qrhead = qr;
  qrtail = qr;
  pthread_spin_unlock (&queuelock);

  /* Wake the main thread so it starts polling stdin.  */
  if (write (wake_pipe[1], "", 1) < 0 && errno != EAGAIN && errno != EINTR)
    /* ignore */;

  return D_SUCCESS;
}

/* Reply to the oldest queued console read, if any, using input from
   host stdin.  Called by the main thread when stdin is readable (or
   at EOF, where read returns 0).  */
static void
read_reply (void)
{
  struct qr *qr;
  ssize_t amtread = 0;
  void *buf = NULL;
  vm_size_t bufsize = 0;
  char inband_buf[IO_INBAND_MAX];

  pthread_spin_lock (&readlock);

  pthread_spin_lock (&queuelock);
  qr = qrhead;
  if (qr)
    {
      qrhead = qr->next;
      if (qrhead == NULL)
        qrtail = NULL;
    }
  pthread_spin_unlock (&queuelock);

  if (! qr)
    {
      pthread_spin_unlock (&readlock);
      return;
    }

  if (qr->type == DEV_READI)
    {
      /* Amounts for in-band reads were validated at enqueue time.  */
      amtread = read (0, inband_buf, qr->amount);
      if (amtread == 0)
	atomic_store_explicit (&stdin_eof, 1, memory_order_relaxed);
      if (amtread < 0)
	send_read_reply (qr, NULL, 0, errno ? errno : EIO);
      else
	send_read_reply (qr, inband_buf, amtread, 0);
    }
  else
    {
      bufsize = qr->amount;
      buf = mmap (0, bufsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (buf == MAP_FAILED)
	{
	  int e = errno ? errno : EIO;
	  pthread_spin_unlock (&readlock);
	  send_read_reply (qr, NULL, 0, e);
	  free (qr);
	  return;
	}
      amtread = read (0, buf, bufsize);
      if (amtread == 0)
	atomic_store_explicit (&stdin_eof, 1, memory_order_relaxed);
      if (amtread > 0 && (vm_size_t) amtread < bufsize)
	{
	  /* Shrink the mapping so the tail pages cannot leak.  */
	  void *nbuf = mmap (0, amtread, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (nbuf != MAP_FAILED)
	    {
	      memcpy (nbuf, buf, amtread);
	      munmap (buf, bufsize);
	      buf = nbuf;
	      bufsize = amtread;
	    }
	}
      if (amtread < 0)
	{
	  int e = errno;
	  munmap (buf, bufsize);
	  pthread_spin_unlock (&readlock);
	  send_read_reply (qr, NULL, 0, e);
	  free (qr);
	  return;
	}
      pthread_spin_unlock (&readlock);
      send_read_reply (qr, buf, amtread, 0);
      munmap (buf, bufsize);
    }

  free (qr);
}

/* Queue an io_select request; satisfied by SELECT_THREAD.  */
static kern_return_t
queue_select (mach_port_t reply_port, mach_msg_type_name_t reply_type,
	      int type, int is_timeout, const struct timespec *ts)
{
  struct selq *sq;

  if (type == 0)
    return 0;

  sq = malloc (sizeof *sq);
  if (! sq)
    return ENOMEM;

  sq->reply_port = reply_port;
  sq->reply_type = reply_type;
  sq->is_timeout = is_timeout;
  sq->type = type;
  sq->next = NULL;
  if (is_timeout)
    {
      if (clock_gettime (CLOCK_MONOTONIC, &sq->deadline) < 0)
	{
	  free (sq);
	  return errno;
	}
      sq->deadline.tv_sec += ts->tv_sec;
      sq->deadline.tv_nsec += ts->tv_nsec;
      if (sq->deadline.tv_nsec >= 1000000000L)
	{
	  sq->deadline.tv_nsec -= 1000000000L;
	  sq->deadline.tv_sec += 1;
	}
    }

  pthread_mutex_lock (&selq_lock);
  if (selq_tail)
    selq_tail->next = sq;
  else
    selq_head = sq;
  selq_tail = sq;
  pthread_mutex_unlock (&selq_lock);

  if (write (select_pipe[1], "", 1) < 0 && errno != EAGAIN && errno != EINTR)
    /* ignore */;

  return MIG_NO_REPLY;
}

static void *
select_thread (void *arg)
{
  pthread_setname_np (pthread_self (), "select");

  for (;;)
    {
      int want_r = 0, want_w = 0, want_x = 0;
      int n, i, npfd = 0;
      int stdin_ready, stdout_ready, urg_ready;
      struct selq *sq, **psq, *done = NULL, **pdone = &done;
      struct pollfd pfd[3];
      int timeout_ms = -1;
      struct timespec now;

      pthread_mutex_lock (&selq_lock);
      for (sq = selq_head; sq; sq = sq->next)
	{
	  if (sq->type & (SELECT_READ | SELECT_URG))
	    want_r = 1;
	  if (sq->type & SELECT_WRITE)
	    want_w = 1;
	  if (sq->type & SELECT_URG)
	    want_x = 1;
	}
      if (selq_head)
	clock_gettime (CLOCK_MONOTONIC, &now);
      for (sq = selq_head; sq; sq = sq->next)
	{
	  if (sq->is_timeout)
	    {
	      long long ms = (sq->deadline.tv_sec - now.tv_sec) * 1000LL
			     + (sq->deadline.tv_nsec - now.tv_nsec) / 1000000LL;
	      int m = ms <= 0 ? 0 : (ms > 0x7fffffffLL ? 0x7fffffff : (int) ms);
	      if (timeout_ms < 0 || m < timeout_ms)
		timeout_ms = m;
	    }
	}
      pthread_mutex_unlock (&selq_lock);

      pfd[npfd].fd = select_pipe[0];
      pfd[npfd].events = POLLIN;
      npfd++;
      if (want_r)
	{
	  pfd[npfd].fd = 0;
	  pfd[npfd].events = POLLIN | (want_x ? POLLPRI : 0);
	  npfd++;
	}
      if (want_w)
	{
	  pfd[npfd].fd = 1;
	  pfd[npfd].events = POLLOUT;
	  npfd++;
	}

      n = poll (pfd, npfd, timeout_ms);
      if (n < 0)
	{
	  if (errno == EINTR)
	    continue;
	  continue;
	}

      if (pfd[0].revents & POLLIN)
	{
	  char c[128];
	  read (select_pipe[0], c, sizeof c);
	}

      stdin_ready = 0, stdout_ready = 0, urg_ready = 0;
      for (i = 1; i < npfd; i++)
	{
	  if ((pfd[i].revents & (POLLIN | POLLHUP | POLLERR))
	      && (pfd[i].events & POLLIN))
	    stdin_ready = 1;
	  if ((pfd[i].revents & POLLOUT) && (pfd[i].events & POLLOUT))
	    stdout_ready = 1;
	  if ((pfd[i].revents & POLLPRI) && (pfd[i].events & POLLPRI))
	    urg_ready = 1;
	}
      if (atomic_load_explicit (&stdin_eof, memory_order_relaxed))
	stdin_ready = 1;

      clock_gettime (CLOCK_MONOTONIC, &now);

      pthread_mutex_lock (&selq_lock);
      for (psq = &selq_head; (sq = *psq); )
	{
	  int result = 0, expired = 0;

	  if (sq->is_timeout
	      && (sq->deadline.tv_sec < now.tv_sec
		  || (sq->deadline.tv_sec == now.tv_sec
		      && sq->deadline.tv_nsec <= now.tv_nsec)))
	    expired = 1;

	  if (! expired)
	    {
	      if (stdin_ready && (sq->type & SELECT_READ))
		result |= SELECT_READ;
	      if (stdout_ready && (sq->type & SELECT_WRITE))
		result |= SELECT_WRITE;
	      if (urg_ready && (sq->type & SELECT_URG))
		result |= SELECT_URG;
	    }

	  if (result || expired)
	    {
	      *psq = sq->next;
	      if (selq_tail == sq)
		selq_tail = NULL;   /* recompute below if needed */
	      sq->type = result;
	      *pdone = sq;
	      pdone = &sq->next;
	      sq->next = NULL;
	    }
	  else
	    psq = &sq->next;
	}
      /* Fix up tail after removals.  */
      if (! selq_head)
	selq_tail = NULL;
      else
	{
	  for (sq = selq_head; sq->next; sq = sq->next)
	    ;
	  selq_tail = sq;
	}
      pthread_mutex_unlock (&selq_lock);

      while ((sq = done))
	{
	  done = sq->next;
	  if (sq->is_timeout)
	    io_select_timeout_reply (sq->reply_port, sq->reply_type, 0,
				     sq->type);
	  else
	    io_select_reply (sq->reply_port, sq->reply_type, 0, sq->type);
	  free (sq);
	}
    }
}


/* Implementation of device interface */

kern_return_t
ds_device_open (mach_port_t master_port,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		const_dev_name_t name,
		mach_port_t *device,
		mach_msg_type_name_t *devicetype)
{
  struct dev_map *map;

  if (master_port != pseudo_master_device_port)
    return D_INVALID_OPERATION;

  if (verbose > 1)
    fprintf (stderr, "Device '%s' being opened.\r\n", name);

  if (!strcmp (name, "console"))
    {
      console_mscount++;
      *device = pseudo_console;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }
  else if (!strcmp (name, "time"))
    {
      *device = pseudo_time;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }
  else if (strcmp (name, "pseudo-root") == 0)
    /* Magic root device.  */
    {
      *device = pseudo_root;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }

  map = lookup_dev (name);
  if (map)
    {
      error_t err;
      file_t node;

      node = file_name_lookup (map->file_name, 0, 0);
      if (! MACH_PORT_VALID (node))
        return D_NO_SUCH_DEVICE;

      *devicetype = MACH_MSG_TYPE_MOVE_SEND;
      err = device_open (node, mode, "", device);
      mach_port_deallocate (mach_task_self (), node);
      return err;
    }

  if (! privileged)
    return D_NO_SUCH_DEVICE;

  *devicetype = MACH_MSG_TYPE_MOVE_SEND;
  return device_open (master_device_port, mode, name, device);
}

kern_return_t
ds_device_open_new (mach_port_t master_port,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		const_dev_name_t name,
		mach_port_t *device,
		mach_msg_type_name_t *devicetype)
{
  return ds_device_open (master_port, reply_port, reply_type, mode,
      name, device, devicetype);
}

kern_return_t
ds_device_close (device_t device)
{
  if (device != pseudo_console && device != pseudo_root && device != pseudo_time)
    return D_NO_SUCH_DEVICE;
  return 0;
}

kern_return_t
ds_device_write (device_t device,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type,
		 dev_mode_t mode,
		 recnum_t recnum,
		 io_buf_ptr_t data,
		 mach_msg_type_number_t datalen,
		 int *bytes_written)
{
  if (device == pseudo_console)
    {
      *bytes_written = write (1, data, datalen);
      if (*bytes_written == -1)
	{
	  if (verbose)
	    fprintf (stderr, "console write: %s\r\n", strerror (errno));
	  return D_IO_ERROR;
	}

      return D_SUCCESS;
    }
  else if (device == pseudo_root)
    {
      size_t wrote;
      if (store_write (root_store, recnum, data, datalen, &wrote) != 0)
	{
	  if (verbose)
	    fprintf (stderr, "store_write: %s\r\n", strerror (errno));
	  return D_IO_ERROR;
	}
      *bytes_written = wrote;
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_write_inband (device_t device,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			dev_mode_t mode,
			recnum_t recnum,
			const io_buf_ptr_inband_t data,
			mach_msg_type_number_t datalen,
			int *bytes_written)
{
  if (device == pseudo_console)
    {
      *bytes_written = write (1, data, datalen);
      if (*bytes_written == -1)
	{
	  if (verbose)
	    fprintf (stderr, "console write: %s\r\n", strerror (errno));
	  return D_IO_ERROR;
	}

      return D_SUCCESS;
    }
  else if (device == pseudo_root)
    {
      size_t wrote;
      if (store_write (root_store, recnum, data, datalen, &wrote) != 0)
	{
	  if (verbose)
	    fprintf (stderr, "store_write: %s\r\n", strerror (errno));
	  return D_IO_ERROR;
	}
      *bytes_written = wrote;
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_read (device_t device,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		recnum_t recnum,
		int bytes_wanted,
		io_buf_ptr_t *data,
		mach_msg_type_number_t *datalen)
{
  error_t err;

  /* Zero-length requests get an immediate empty answer.  */
  if (bytes_wanted == 0)
    {
      *data = 0;
      *datalen = 0;
      return D_SUCCESS;
    }

  if (device == pseudo_console)
    {
      int avail;

      if (bytes_wanted < 0)
	return D_INVALID_SIZE;
      if (bytes_wanted > CONSOLE_READ_MAX)
	bytes_wanted = CONSOLE_READ_MAX;

      pthread_spin_lock (&readlock);
      if (ioctl (0, FIONREAD, &avail) < 0)
	{
	  pthread_spin_unlock (&readlock);
	  return errno;
	}
      if (avail)
	{
	  void *new_data = mmap (0, bytes_wanted, PROT_READ|PROT_WRITE,
				 MAP_ANON, 0, 0);
	  if (new_data == MAP_FAILED)
	    {
	      pthread_spin_unlock (&readlock);
	      return errno;
	    }
	  *data = new_data;
	  *datalen = read (0, *data, bytes_wanted);
	  if (*datalen == 0)
	    atomic_store_explicit (&stdin_eof, 1, memory_order_relaxed);
	  pthread_spin_unlock (&readlock);
	  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
	}
      else
	{
	  pthread_spin_unlock (&readlock);
	  err = queue_read (DEV_READ, reply_port, reply_type,
			    (vm_size_t) bytes_wanted);
	  return err == D_SUCCESS ? MIG_NO_REPLY : err;
	}
    }
  else if (device == pseudo_root)
    {
      size_t data_size = 0;
      err = store_read (root_store, recnum, bytes_wanted, (void **)data, &data_size);
      if (err)
	{
	  if (verbose)
	    fprintf (stderr, "store_read: %s\r\n", strerror (err));
	  return D_IO_ERROR;
	}
      *datalen = data_size;
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_read_inband (device_t device,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_type,
		       dev_mode_t mode,
		       recnum_t recnum,
		       int bytes_wanted,
		       io_buf_ptr_inband_t data,
		       mach_msg_type_number_t *datalen)
{
  /* The buffer is a fixed MIG in-band array; bound the request.  */
  if (bytes_wanted < 0 || bytes_wanted > IO_INBAND_MAX)
    return D_INVALID_SIZE;
  if (bytes_wanted == 0)
    {
      *datalen = 0;
      return D_SUCCESS;
    }

  if (device == pseudo_console)
    {
      int avail;

      pthread_spin_lock (&readlock);
      if (ioctl (0, FIONREAD, &avail) < 0)
	{
	  pthread_spin_unlock (&readlock);
	  return errno;
	}
      if (avail)
	{
	  *datalen = read (0, data, bytes_wanted);
	  if (*datalen == 0)
	    atomic_store_explicit (&stdin_eof, 1, memory_order_relaxed);
	  pthread_spin_unlock (&readlock);
	  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
	}
      else
	{
	  kern_return_t err;

	  pthread_spin_unlock (&readlock);
	  err = queue_read (DEV_READI, reply_port, reply_type,
			    (vm_size_t) bytes_wanted);
	  return err == D_SUCCESS ? MIG_NO_REPLY : err;
	}
    }
  else if (device == pseudo_root)
    {
      error_t err;
      void *returned = data;
      size_t data_size = bytes_wanted;

      err = store_read (root_store, recnum, bytes_wanted,
			(void **)&returned, &data_size);
      *datalen = data_size;

      if (! err)
	{
	  if (returned != data)
	    {
	      memcpy ((void *)data, returned, data_size);
	      munmap ((caddr_t) returned, data_size);
	    }
	  return D_SUCCESS;
	}
      else
	return D_IO_ERROR;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_map (device_t device,
	       vm_prot_t prot,
	       vm_offset_t offset,
	       vm_size_t size,
	       memory_object_t *pager,
	       int unmap)
{
  if (device == pseudo_console || device == pseudo_root)
    return D_INVALID_OPERATION;
  else if (device == pseudo_time)
    {
      error_t err;
      mach_port_t wr_memobj;
      file_t node = file_name_lookup ("/dev/time", O_RDONLY, 0);

      if (node == MACH_PORT_NULL)
	return D_IO_ERROR;

      err = io_map (node, pager, &wr_memobj);
      if (err)
	{
	  mach_port_deallocate (mach_task_self (), node);
	  *pager = MACH_PORT_NULL;
	  return D_IO_ERROR;
	}
      if (MACH_PORT_VALID (wr_memobj))
	mach_port_deallocate (mach_task_self (), wr_memobj);

      mach_port_deallocate (mach_task_self (), node);
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_set_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      mach_msg_type_number_t statuslen)
{
  if (device != pseudo_console && device != pseudo_root)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_get_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      mach_msg_type_number_t *statuslen)
{
  if (device == pseudo_console)
    return D_INVALID_OPERATION;
  else if (device == pseudo_root)
    switch (flavor)
      {
      case DEV_GET_SIZE:
        if (*statuslen < DEV_GET_SIZE_COUNT)
          return D_INVALID_SIZE;
	if (root_store->size > UINT32_MAX
	    || root_store->block_size > UINT32_MAX)
	  return D_INVALID_SIZE;
        status[DEV_GET_SIZE_DEVICE_SIZE] = root_store->size;
        status[DEV_GET_SIZE_RECORD_SIZE] = root_store->block_size;
        *statuslen = DEV_GET_SIZE_COUNT;
        return D_SUCCESS;

      case DEV_GET_RECORDS:
        if (*statuslen < DEV_GET_RECORDS_COUNT)
          return D_INVALID_SIZE;
	if (root_store->blocks > UINT32_MAX
	    || root_store->block_size > UINT32_MAX)
	  return D_INVALID_SIZE;
        status[DEV_GET_RECORDS_DEVICE_RECORDS] = root_store->blocks;
        status[DEV_GET_RECORDS_RECORD_SIZE] = root_store->block_size;
        *statuslen = DEV_GET_RECORDS_COUNT;
        return D_SUCCESS;

      default:
        return D_INVALID_OPERATION;
      }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_set_filter (device_t device,
		      mach_port_t receive_port,
		      int priority,
		      filter_array_t filter,
		      mach_msg_type_number_t filterlen)
{
  if (device != pseudo_console && device != pseudo_root)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_intr_register (device_t dev,
			 int id,
			 int flags,
			 mach_port_t receive_port)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_intr_ack (device_t dev,
		    mach_port_t receive_port)
{
  return D_INVALID_OPERATION;
}


/* Implementation of notify interface */
kern_return_t
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (mach_port_t notify,
			   mach_port_mscount_t mscount)
{
  static int no_console;
  mach_port_t foo;
  if (notify == pseudo_master_device_port)
    {
      if (no_console)
	goto bye;
      pseudo_master_device_port = MACH_PORT_NULL;
      return 0;
    }
  if (notify == pseudo_console)
    {
      if (mscount == console_mscount &&
	  pseudo_master_device_port == MACH_PORT_NULL)
	{
	bye:
	  restore_termstate ();
	  write_diag ("bye\n", 4);
	  host_exit (0);
	}
      else
	{
	  no_console = (mscount == console_mscount);

	  mach_port_request_notification (mach_task_self (), pseudo_console,
					  MACH_NOTIFY_NO_SENDERS,
					  console_mscount == mscount
					  ? mscount + 1
					  : console_mscount,
					  pseudo_console,
					  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
	  if (foo != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), foo);
	}
      return 0;
    }

  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return EOPNOTSUPP;
}

static void task_died (mach_port_t name);

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  if (notify != dead_task_notification_port)
    return EOPNOTSUPP;
  task_died (name);
  mach_port_deallocate (mach_task_self (), name);
  return 0;
}


/* Implementation of the Hurd I/O interface, which
   we support for the console port only. */

kern_return_t
S_io_write (mach_port_t object,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    const_data_t data,
	    mach_msg_type_number_t datalen,
	    off_t offset,
	    vm_size_t *amtwritten)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

  *amtwritten = write (1, data, datalen);
  return *amtwritten == -1 ? errno : 0;
}

kern_return_t
S_io_read (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   data_t *data,
	   mach_msg_type_number_t *datalen,
	   off_t offset,
	   vm_size_t amount)
{
  mach_msg_type_number_t avail;

  if (object != pseudo_console)
    return EOPNOTSUPP;

  if (amount > CONSOLE_READ_MAX)
    amount = CONSOLE_READ_MAX;
  if (amount == 0)
    {
      *datalen = 0;
      return 0;
    }

  pthread_spin_lock (&readlock);
  if (ioctl (0, FIONREAD, &avail) < 0)
    {
      pthread_spin_unlock (&readlock);
      return errno;
    }
  if (avail)
    {
      data_t orig_data = *data;
      if (amount > *datalen)
	{
	  void *new_data = mmap (0, amount, PROT_READ|PROT_WRITE,
				 MAP_ANON, 0, 0);
	  if (new_data == MAP_FAILED)
	    {
	      pthread_spin_unlock (&readlock);
	      return errno;
	    }

	  *data = new_data;
        }
      *datalen = read (0, *data, amount);
      if (*datalen == 0)
	atomic_store_explicit (&stdin_eof, 1, memory_order_relaxed);
      if (*datalen == -1 && *data != orig_data)
	munmap (*data, amount);
      pthread_spin_unlock (&readlock);
      return *datalen == -1 ? errno : 0;
    }
  else
    {
      kern_return_t err;
      pthread_spin_unlock (&readlock);
      err = queue_read (IO_READ, reply_port, reply_type, amount);
      return err == D_SUCCESS ? MIG_NO_REPLY : err;
    }
}

kern_return_t
S_io_seek (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   off_t offset,
	   int whence,
	   off_t *newp)
{
  return object == pseudo_console ? ESPIPE : EOPNOTSUPP;
}

kern_return_t
S_io_readable (mach_port_t object,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       vm_size_t *amt)
{
  int avail;

  if (object != pseudo_console)
    return EOPNOTSUPP;
  if (ioctl (0, FIONREAD, &avail) < 0)
    return errno;
  *amt = avail;
  return 0;
}

kern_return_t
S_io_set_all_openmodes (mach_port_t object,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_openmodes (mach_port_t object,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type,
		    int *modes)
{
  *modes = O_READ | O_WRITE;
  return object == pseudo_console ? 0 : EOPNOTSUPP;
}

kern_return_t
S_io_set_some_openmodes (mach_port_t object,
			 mach_port_t reply_port,
			 mach_msg_type_name_t reply_type,
			 int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_clear_some_openmodes (mach_port_t object,
			   mach_port_t reply_port,
			   mach_msg_type_name_t reply_type,
			   int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_async (mach_port_t object,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    mach_port_t notify,
	    mach_port_t *id,
	    mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_mod_owner (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		pid_t owner)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_owner (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		pid_t *owner)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_icky_async_id (mach_port_t object,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			mach_port_t *id,
			mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_select (mach_port_t object,
	     mach_port_t reply_port,
	     mach_msg_type_name_t reply_type,
	     int *type)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

  return queue_select (reply_port, reply_type, *type, 0, NULL);
}

kern_return_t
S_io_select_timeout (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     struct timespec ts,
		     int *type)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

  if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
    return EINVAL;

  return queue_select (reply_port, reply_type, *type, 1, &ts);
}

kern_return_t
S_io_stat (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   struct stat *st)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

  memset (st, 0, sizeof(struct stat));
  st->st_mode = S_IFCHR | 0666;
  st->st_blksize = 1024;
  return 0;
}

kern_return_t
S_io_reauthenticate (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     mach_port_t rend)
{
  uid_t *gu, *au;
  gid_t *gg, *ag;
  mach_msg_type_number_t gulen = 0, aulen = 0, gglen = 0, aglen = 0;
  error_t err;

  if (object != pseudo_console)
    return EOPNOTSUPP;

  /* Without an auth server there is nobody to reauthenticate
     against.  */
  if (authserver == MACH_PORT_NULL)
    return EOPNOTSUPP;

  err = mach_port_insert_right (mach_task_self (), object, object,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    return err;

  do
    err = auth_server_authenticate (authserver,
				  rend, MACH_MSG_TYPE_COPY_SEND,
				  object, MACH_MSG_TYPE_COPY_SEND,
				  &gu, &gulen,
				  &au, &aulen,
				  &gg, &gglen,
				  &ag, &aglen);
  while (err == EINTR);

  if (! err)
    {
      mig_deallocate ((vm_address_t) gu, gulen * sizeof *gu);
      mig_deallocate ((vm_address_t) au, aulen * sizeof *au);
      mig_deallocate ((vm_address_t) gg, gglen * sizeof *gg);
      mig_deallocate ((vm_address_t) ag, aglen * sizeof *ag);
    }
  mach_port_deallocate (mach_task_self (), rend);
  mach_port_deallocate (mach_task_self (), object);

  return err;
}

kern_return_t
S_io_restrict_auth (mach_port_t object,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type,
		    mach_port_t *newobject,
		    mach_msg_type_name_t *newobjtype,
		    const uid_t *uids,
		    mach_msg_type_number_t nuids,
		    const uid_t *gids,
		    mach_msg_type_number_t ngids)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  *newobject = pseudo_console;
  *newobjtype = MACH_MSG_TYPE_MAKE_SEND;
  console_mscount++;
  return 0;
}

kern_return_t
S_io_duplicate (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		mach_port_t *newobj,
		mach_msg_type_name_t *newobjtype)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  *newobj = pseudo_console;
  *newobjtype = MACH_MSG_TYPE_MAKE_SEND;
  console_mscount++;
  return 0;
}

kern_return_t
S_io_server_version (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     string_t name,
		     int *maj,
		     int *min,
		     int *edit)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_map (mach_port_t obj,
	  mach_port_t reply_port,
	  mach_msg_type_name_t reply_type,
	  mach_port_t *rd,
	  mach_msg_type_name_t *rdtype,
	  mach_port_t *wr,
	  mach_msg_type_name_t *wrtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_map_cntl (mach_port_t obj,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       mach_port_t *mem,
	       mach_msg_type_name_t *memtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_conch (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_release_conch (mach_port_t obj,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_eofnotify (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)

{
  return EOPNOTSUPP;
}

kern_return_t
S_io_prenotify (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		vm_offset_t start,
		vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_postnotify (mach_port_t obj,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type,
		 vm_offset_t start,
		 vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_readsleep (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_readnotify (mach_port_t obj,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}


kern_return_t
S_io_sigio (mach_port_t obj,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}


kern_return_t
S_io_pathconf (mach_port_t obj,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       int name, int *value)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_identity (mach_port_t obj,
	       mach_port_t reply,
	       mach_msg_type_name_t replytype,
	       mach_port_t *id,
	       mach_msg_type_name_t *idtype,
	       mach_port_t *fsid,
	       mach_msg_type_name_t *fsidtype,
	       ino_t *fileno)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_revoke (mach_port_t obj,
	     mach_port_t reply, mach_msg_type_name_t replyPoly)
{
  return EOPNOTSUPP;
}



/* Implementation of the Hurd terminal driver interface, which we only
   support on the console device.  */

kern_return_t
S_termctty_open_terminal (ctty_t object,
			  int flags,
			  mach_port_t *result,
			  mach_msg_type_name_t *restype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_term_getctty (mach_port_t object,
		mach_port_t *cttyid, mach_msg_type_name_t *cttyPoly)
{
  static mach_port_t id = MACH_PORT_NULL;

  if (object != pseudo_console)
    return EOPNOTSUPP;

  if (id == MACH_PORT_NULL)
    mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_DEAD_NAME, &id);

  *cttyid = id;
  *cttyPoly = MACH_MSG_TYPE_COPY_SEND;
  return 0;
}


kern_return_t S_term_open_ctty
(
	io_t terminal,
	pid_t pid,
	pid_t pgrp,
	mach_port_t *newtty,
	mach_msg_type_name_t *newttytype
)
{ return EOPNOTSUPP; }

kern_return_t S_term_set_nodename
(
	io_t terminal,
	const_string_t name
)
{ return EOPNOTSUPP; }

kern_return_t S_term_get_nodename
(
	io_t terminal,
	string_t name
)
{ return EOPNOTSUPP; }

kern_return_t S_term_get_peername
(
	io_t terminal,
	string_t name
)
{ return EOPNOTSUPP; }

kern_return_t S_term_set_filenode
(
	io_t terminal,
	file_t filenode
)
{ return EOPNOTSUPP; }

kern_return_t S_term_get_bottom_type
(
	io_t terminal,
	int *ttype
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_machdev
(
	io_t terminal,
	mach_port_t machdev
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_hurddev
(
	io_t terminal,
	io_t hurddev
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_pty
(
	io_t terminal,
	io_t *ptymaster
)
{ return EOPNOTSUPP; }

/* Mach host emulation.  */

kern_return_t
S_vm_set_default_memory_manager (mach_port_t host_priv,
                                 mach_port_t *default_manager)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  if (*default_manager != MACH_PORT_NULL)
    return KERN_INVALID_ARGUMENT;

  *default_manager = MACH_PORT_NULL;
  return KERN_SUCCESS;
}

kern_return_t
S_host_reboot (mach_port_t host_priv,
               int flags)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  fprintf (stderr, "Would %s the system.  Bye.\r\n",
           flags & RB_HALT? "halt": "reboot");
  host_exit (0);
}


kern_return_t
S_host_processor_set_priv (mach_port_t host_priv,
			   mach_port_t set_name,
			   mach_port_t *set)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  *set = pseudo_pset;
  return KERN_SUCCESS;
}

kern_return_t
S_register_new_task_notification (mach_port_t host_priv,
				  mach_port_t notification)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  if (! MACH_PORT_VALID (notification))
    return KERN_INVALID_ARGUMENT;

  if (MACH_PORT_VALID (new_task_notification))
    return KERN_NO_ACCESS;

  new_task_notification = notification;
  return KERN_SUCCESS;
}


/* Managing tasks.  */

static void
task_ihash_cleanup (hurd_ihash_value_t value, void *cookie)
{
  (void) cookie;
  mach_port_deallocate (mach_task_self (), (mach_port_t)(uintptr_t) value);
}

static struct hurd_ihash task_ihash =
  HURD_IHASH_INITIALIZER_GKI (HURD_IHASH_NO_LOCP, task_ihash_cleanup, NULL,
                              NULL, NULL);

static void
task_died (mach_port_t name)
{
  if (verbose > 1)
    fprintf (stderr, "Task '%lu' died.\r\n", (unsigned long) name);

  hurd_ihash_remove (&task_ihash, (hurd_ihash_key_t) name);
}

/* Handle new task notifications from proc.  */
kern_return_t
S_mach_notify_new_task (mach_port_t notify,
			mach_port_t task,
			mach_port_t parent)
{
  error_t err;
  mach_port_t previous;

  if (notify != task_notification_port)
    return EOPNOTSUPP;

  if (verbose > 1)
    fprintf (stderr, "Task '%lu' created by task '%lu'.\r\n",
	     (unsigned long) task, (unsigned long) parent);

  err = mach_port_request_notification (mach_task_self (), task,
                                        MACH_NOTIFY_DEAD_NAME, 0,
                                        dead_task_notification_port,
                                        MACH_MSG_TYPE_MAKE_SEND_ONCE,
                                        &previous);
  if (err)
    goto fail;
  assert_backtrace (! MACH_PORT_VALID (previous));

  err = mach_port_mod_refs (mach_task_self (), task, MACH_PORT_RIGHT_SEND,
			    +1);
  if (err)
    goto fail;
  err = hurd_ihash_add (&task_ihash,
                        (hurd_ihash_key_t) task,
			(hurd_ihash_value_t)(uintptr_t) task);
  if (err)
    goto fail;

  if (MACH_PORT_VALID (new_task_notification))
    /* Relay the notification.  This consumes task and parent.  */
    return mach_notify_new_task (new_task_notification, task, parent);

  mach_port_deallocate (mach_task_self (), task);
  mach_port_deallocate (mach_task_self (), parent);
  return 0;

 fail:
  task_terminate (task);
  mach_port_deallocate (mach_task_self (), task);
  mach_port_deallocate (mach_task_self (), parent);
  return err;
}

kern_return_t
S_processor_set_tasks(mach_port_t processor_set,
		      task_array_t *task_list,
		      mach_msg_type_number_t *task_listCnt)
{
  error_t err;
  size_t i, count;
  int kernel_in_hash = 0;
  hurd_ihash_value_t value;

  if (processor_set != pseudo_pset)
    return KERN_INVALID_ARGUMENT;

  if (! MACH_PORT_VALID (pseudo_kernel))
    {
      *task_listCnt = 0;
      return 0;
    }

  HURD_IHASH_ITERATE (&task_ihash, value)
    if ((task_t) (uintptr_t) value == pseudo_kernel)
      kernel_in_hash = 1;

  count = task_ihash.nr_items + (kernel_in_hash ? 0 : 1);
  if (count > SIZE_MAX / sizeof **task_list)
    return KERN_RESOURCE_SHORTAGE;

  err = vm_allocate (mach_task_self (), (vm_address_t *) task_list,
		     count * sizeof **task_list, 1);
  if (err)
    return err;

  /* The first task has to be the kernel.  */
  (*task_list)[0] = pseudo_kernel;

  i = 1;
  HURD_IHASH_ITERATE (&task_ihash, value)
    {
      task_t task = (task_t) (uintptr_t) value;
      if (task == pseudo_kernel)
	continue;

      (*task_list)[i] = task;
      i += 1;
    }

  *task_listCnt = i;
  return 0;
}
