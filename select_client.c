/**
Author:ZUHD
Date:2014-10-14
*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#define MAX_MSG_LEN 1024 * 10
int g_running = 1;
int is_connected = 0;
char read_buff[MAX_MSG_LEN] = {0};
char write_buff[MAX_MSG_LEN] = {0};
int read_offset = 0;
int write_offset = 0;

void exit_signal(int sig)
{
	g_running = 0;
	return;
}

int setnonblocking(int fd)
{
	int flag = fcntl(fd, F_GETFL, 0);
	if (flag < 0)
	{
		return -1;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

int read_cb(int fd)
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
			// 通知业务层 断开连接
			return -1;
		}
	}
	else if (len == 0)
	{
		// 通知业务层断开连接
		printf("ondisconnection\n");
		return -1;
	}
	else
	{
		read_offset += len;
		// 通知业务层 处理业务逻辑
		printf("recv data=%s\n", read_buff);
	}
	return len;
}

int write_cb(int fd)
{
	if (write_offset < 0 ||
	write_offset >= MAX_MSG_LEN)
	{
		fprintf(stderr, "write_offset error\n");
		return -1;
	}
	
	if (write_offset == 0)
	{
		return 0;
	}
	
	int len = write(fd, write_buff, write_offset);
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
			return -1;
		}
	}
	else if (len == 0)
	{
		// 通知业务层断开连接
		return -1;
	}
	else
	{
		memmove(write_buff, write_buff + len, write_offset - len);
		write_offset -= len;
		printf("send data\n");
	}
	return len;
}


int select_work(int fd)
{
	fd_set read_set, write_set, err_set;
	FD_ZERO(&read_set);
	FD_SET(fd, &read_set);
	write_set = read_set;
	err_set = read_set;
	struct timeval tval;
	tval.tv_sec = 0;
	tval.tv_usec = 200 * 1000;	// 200毫秒
	int ready_n = 0;
	ready_n = select(fd + 1, &read_set, &write_set, &err_set, &tval);
	if (ready_n == 0)
	{
		// 没有数据到达
		printf("no data to deal\n");
		return 0;
	}	
	else if (ready_n < 0)
	{
		perror("select error");
		return -1;
	}
	else
	{
		// 可写中也有可能是连接成功
		// 把写缓冲区数据发送出去
		// 最后一次的握手协议发送出去
		if (FD_ISSET(fd, &write_set))
		{
			if (!is_connected)
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
					// 业务层的连接成功回调
					is_connected = 1;
					// 给服务器发消息
					printf("onconnection later\n");
					char* hello = "hello";
					memcpy(write_buff + write_offset, hello, strlen(hello));
					write_offset += strlen(hello);
				}						
			}
			else
			{
				if (write_cb(fd) < 0)
				{
					return -1;
				}
			}			
		}
		
		if (FD_ISSET(fd, &read_set))
		{
			if (read_cb(fd) < 0)
			{
				return -1;
			}
		}
	}
	return ready_n;
}

int main(int argc, char** argv)
{
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) < 0 ||
	sigaction(SIGPIPE, &sa, 0) < 0)
	{
		perror("sigaction error");
		return -1;
	}
	signal(SIGTERM, exit_signal);
	struct sockaddr_in server_addr;
	int clientfd;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr("192.168.17.101");
	server_addr.sin_port = htons(6666);
	
	// 创建socket
	// 设置socket
	// 连接服务器
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
	
	ret = connect(clientfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr));
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
		is_connected = 1;
		printf("onconnection now\n");
	}
	
	while(g_running)
	{
		int ret = select_work(clientfd);
		if (ret < 0)
		{
			return -1;
		}
		else if (ret == 0)
		{
			usleep(100);
		}
	}	
	return 0;
}