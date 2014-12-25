/*
@Author:ZUHD
@Data:2014-10-20
*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#define MAX_MSG_LEN 1024 * 10
int g_running = 1;
int backlog = 1024;
char read_buff[MAX_MSG_LEN] = {0};
char write_buff[MAX_MSG_LEN] = {0};
int read_offset = 0;
int write_offset = 0;


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
		perror("setnonblocking error");
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
		fprintf(stderr, "read error\n");
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
			// 调用业务层逻辑，断开连接
			perror("read error");
			return -1;
		}
	}
	else if (len == 0)
	{
		// 调用业务层逻辑，断开连接
		printf("on disconnection\n");
		return -1;
	}
	else
	{
		read_offset += len;
		printf("recv data=%s\n", read_buff);
	}
	return len;
}

int write_cb(int fd)
{
	if (write_offset < 0 ||
	write_offset >= MAX_MSG_LEN)
	{
		perror("write_offset error");
		return -1;
	}
	
	if (write_offset == 0)
	{
		// nothing to send
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

int select_work(int listenfd)
{	
	int fdcount = 0;
	int sock[FD_SETSIZE] = {0};
	sock[fdcount++] = listenfd;
	
	while(1)
	{
		fd_set read_set, write_set, error_set;
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		FD_ZERO(&error_set);		
	
		struct timeval tval;
		tval.tv_sec = 0;
		tval.tv_usec = 100 * 1000;	// 100ms		
		
		int ready = 0;
		int i = 0;
		int nfds = 0;
		for (i = 0; i < fdcount; i++)
		{
			if (sock[i] > 0)
			{
				FD_SET(sock[i], &read_set);
				FD_SET(sock[i], &write_set);
				FD_SET(sock[i], &error_set);
			}
			if (sock[i] > nfds)
			{
				nfds = sock[i];
			}			
		}
		ready = select(nfds + 1, &read_set, &write_set, &error_set, &tval);
		if (ready < 0)
		{
			perror("select error");
			return -1;
		}
		else if (ready == 0)
		{
			//printf("no select events\n");
			usleep(100);
		}
		else
		{
			for (i = 0; i < fdcount; i++)
			{
					if (FD_ISSET(sock[i], &write_set) && sock[i] > 0)
					{
						if (write_cb(sock[i]) < 0)
						{
							sock[i] = 0;
						}
					}
					if (FD_ISSET(sock[i], &read_set) && sock[i] > 0)
					{
						if (sock[i] == listenfd)
						{
							// accept events
							struct sockaddr_in client_addr;
							bzero(&client_addr, sizeof(client_addr));
							socklen_t socklen = sizeof(struct sockaddr_in);
							int fd = accept(listenfd, (struct sockaddr*)&client_addr, &socklen);
							if (fd < 0)
							{
								perror("accept error");
								return -1;
							}
							printf("accept client from[%s]\n", inet_ntoa(client_addr.sin_addr));
							sock[fdcount++] = fd;
							FD_SET(fd, &read_set);
						}
						else
						{
							if (read_cb(sock[i]) < 0)
							{
								sock[i] = 0;
							}
						}						
					}
			}			
		}
	}
	return 0;
}

int main(int argc, char** argv)
{
	int listenfd;
	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr("192.168.17.101");
	server_addr.sin_port = ntohs(6666);
	
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0)
	{
		perror("create socket error");
		return -1;
	}
	
	// 设置复用
	int opt = 1; // true
	int ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0)
	{
		perror("setsockopt error");
		return -1;
	}
	
	ret = setnonblocking(listenfd);
	if (ret < 0)
	{
		perror("setnonblocking error");
		return -1;
	}
	
	ret = bind(listenfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr));
	if (ret < 0)
	{
		perror("bind error");
		return -1;
	}
	
	// listen
	ret = listen(listenfd, backlog);
	if (ret < 0)
	{
		perror("listen error");
		return -1;
	}
	
	signal(SIGTERM, exit_signal);
	return select_work(listenfd);
}