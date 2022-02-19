#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <asm/types.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/route.h>

//#define t_assert(x) { \
//	if(!(x))  {err = -__LINE__;goto error;} \
//}

int android_exec(const char * cmdstring) {
  int status;
  pid_t pid;

  if(cmdstring == NULL) {
    return (1);
  }

  pid = fork();

  if(pid < 0) {
    status = -1;
  } else if(pid == 0) {
    execl("/system/bin/sh", "sh", "-c", cmdstring, (char *) NULL);
    _exit(127);
  } else {
    while(waitpid(pid, &status, 0) == -1) {
      if(errno != EINTR) {
        status = -1;
        break;
      }
    }
  }
  return status;
}

int linux_type() {
  if((access("/bin/sh",F_OK))!=-1) {
    return 0;
  }  else if((access("/system/bin/sh",F_OK))!=-1) {
    return 1;
  } else {
    return -1;
  }
}

int rta_gateway(struct nlmsghdr *nh) {
  int len;
  struct rtattr *attr;
  struct rtmsg *rt;
  rt = NLMSG_DATA(nh);
  attr = RTM_RTA(rt);
  len = nh->nlmsg_len - NLMSG_SPACE(sizeof(*rt));

  for(; RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
    if(attr->rta_type == RTA_GATEWAY) {
      return 1;
    }
  }
  return 0;
}

const char * find_outbound(char * remote_ip) {
  int sockfd;
  struct sockaddr_in remote, local;
  int len = sizeof(local);
  char buf[INET_ADDRSTRLEN];

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(65535);

  if((inet_pton(AF_INET, remote_ip, &remote.sin_addr) == 1) && \
      (connect(sockfd, (struct sockaddr *) &remote, sizeof(remote)) == 0) && \
      (getsockname(sockfd, (struct sockaddr *) &local, &len) == 0)) {
    close(sockfd);
    return inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
  }
  close(sockfd);
  return "";
}

int print_outbound() {
  char outbound[INET_ADDRSTRLEN] = "";
  memcpy(outbound, find_outbound("8.8.8.8"), INET_ADDRSTRLEN);
  if(strlen(outbound)==0) {
    return 1;
  } else {
    printf("%s\n",outbound);
  }
  return 0;
}

int trigger_handle(const char * handle_script) {
  int exec_type;
  exec_type = linux_type();
  if(exec_type == 0) {
    return system(handle_script);
  } else if(exec_type == 1) {
    return android_exec(handle_script);
  } else {
    return -1;
  }
}

int watcher_daemon(const char * handle_script) {
  int err = 0;
  int read_buf;
  int socket_fd;
  fd_set rd_set;
  struct nlmsghdr *nh;
  struct sockaddr_nl sa;

  int len = 20480;
  char buff[2048];

  char this_outbound[INET_ADDRSTRLEN] = "";
  char last_outbound[INET_ADDRSTRLEN] = "";

  socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if ((socket_fd < 0) || (setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &len, sizeof(len)))) {
    return 1;
  }
  // t_assert(socket_fd > 0);
  // t_assert(!setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &len, sizeof(len)));

  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = RTMGRP_IPV4_ROUTE;
  if (bind(socket_fd, (struct sockaddr *) &sa, sizeof(sa))) {
    return 1;
  }
  // t_assert(!bind(socket_fd, (struct sockaddr *) &sa, sizeof(sa)));

  FD_ZERO(&rd_set);
  FD_SET(socket_fd, &rd_set);

  trigger_handle(handle_script);

  memcpy(last_outbound, find_outbound("8.8.8.8"), INET_ADDRSTRLEN);

  while((read_buf = read(socket_fd, buff, sizeof(buff))) > 0) {
    for(nh = (struct nlmsghdr *) buff; NLMSG_OK(nh, read_buf); nh = NLMSG_NEXT(nh, read_buf)) {
      if (nh->nlmsg_type == RTM_NEWROUTE) {
        memcpy(this_outbound, find_outbound("8.8.8.8"), INET_ADDRSTRLEN);
        if ((strlen(this_outbound)>0) && (strcmp(this_outbound,last_outbound)!=0)) {
          memcpy(last_outbound, this_outbound, INET_ADDRSTRLEN);
          trigger_handle(handle_script);
        }
      }
    }
  }

  close(socket_fd);
/*
error:
  if(err < 0) {
    printf("Error at line %d\nErrno=%d\n", -err, errno);
  }
  return err;
*/
}

int main(int argc, char *argv[]) {

  int opt;
  char *optstr = "d:o";

  while((opt = getopt(argc, argv, optstr)) != -1) {
    switch(opt) {
    case 'o':
      return print_outbound();
    case 'd':
      return watcher_daemon(optarg);
    case '?':
      fprintf(stderr, "Usage: %s [-o|-d /path/to/handle_script.sh]\n", argv[0]);
      return 1;
    }
  }

}
