/*
@Author:ZUHD
@Data:2014-10-21
*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int g_running = 1;
int max_fd = 1024;
#define MAX_MSG_LEN 1024*10
char read_buff[MAX_MSG_LEN] = {0};
char write_buff[MAX_MSG_LEN] = {0};
int read_offset = 0;
int write_offset = 0;
int isconnected = 0;
void exit_signal(int signum)
{
		g_running = 0;
		return;
}

int setnonblocking(int fd)
{
	int flag = fcntl(fd, F_GETFL, 0);
	if (flag < 0)
	{
		perror("set nonblocking error");
		return -1;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

int read_cb(int epoll_fd, int fd)
{
	if (read_offset < 0 ||
	read_offset >= MAX_MSG_LEN)
	{
		fprintf(stderr, "read_offset error\n");
		return -1;
	}
	
	int len = read(fd, read_buff + read_offset, MAX_MSG_LEN - read_offset);
	if (len < 0)
	{
		if (errno == EAGAIN ||
		errno == EINTR)
		{
			return 0;
		}
		else
		{
			// 通知业务层断开连接
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
			return -1;
		}
	}
	else if (len == 0)
	{
		// 通知业务层断开连接
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		return -1;
	}
	else
	{
		read_offset += len;
		printf("recv data\n");
	}
	return len;
}

int write_cb(int epoll_fd, int fd)
{
	if (write_offset < 0 ||
	write_offset >= MAX_MSG_LEN)
	{
		fprintf(stderr, "write_offset error");
		return -1;
	}
	
	if (write_offset == 0)
	{
		return 0;
	}
	
	int len = write(fd, write_buff, MAX_MSG_LEN - write_offset);
	if (len < 0)
	{
		if (!isconnected)
		{
			int err_ret;
			socklen_t len = sizeof(err_ret);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err_ret, &len) < 0)
			{
				perror("getsockopt error");
				return -1;
			}			
			if (err_ret == 0 ||
			err_ret == EINPROGRESS)
			{
				// 业务层回调成功
				isconnected = 1;
				printf("on connection later\n");
			}
			else
			{
			}
		}
		else
		{
			if (errno == EAGAIN ||
			errno == EINTR)
			{
				return 0;
			}
			else
			{
				// 通知业务层断开连接
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
				return -1;
			}
		}
	}
	else if (len == 0)
	{
		// 通知业务层断开连接
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		return -1;
	}
	else
	{
		memmove(write_buff, write_buff + len, write_offset - len);
		write_offset -= len;
	}
	return len;
}

int epoll_work(int epoll_fd)
{
	return 0;
}

int main(int argc, char** argv)
{
	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr("192.168.17.101");
	server_addr.sin_port = ntohs(6666);
	
	int clientfd;
	clientfd = socket(AF_INET, SOCK_STREAM, 0);
	if (clientfd < 0)
	{
		perror("create socket error");
		return -1;
	}
	
	int ret = setnonblocking(clientfd);
	if (ret < 0)
	{
		perror("setnonblocking error");
		return -1;
	}
	
	ret = connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if (ret < 0)
	{
		if (errno != EINPROGRESS)
		{
			perror("connect error");
			return -1;
		}
	}
	else if (ret == 0)
	{
		isconnected = 1;
		printf("on connection now\n");
	}
	
	struct epoll_event ev;
	int epoll_fd = epoll_create(max_fd);
	ev.events = EPOLLIN;
	ev.data.fd = clientfd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &ev);
	if (ret < 0)
	{
		perror("epoll ctl clientfd error");
		return -1;
	}
	
	signal(SIGTERM, exit_signal);
	
	while(g_running)
	{
		struct epoll_event events[1024];
		int nfds = epoll_wait(epoll_fd, events, max_fd, -1);
		if (nfds < 0)
		{
			perror("epoll_wait error");
			return -1;
		}
		else if (nfds == 0)
		{
			usleep(100);
		}
		else
		{
			int i = 0;
			for ( i = 0; i < nfds; i++)
			{
				if (events[i].data.fd & EPOLLIN)
				{
					read_cb(epoll_fd, events[i].data.fd);
				}
				
				if (events[i].data.fd & EPOLLOUT)
				{
					write_cb(epoll_fd, events[i].data.fd);
				}
			}
		}
	}
}