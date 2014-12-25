/**
@Author:ZUHD
@Date:2014-10-11
*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

// 0������ϵͳ��Դ���ƣ�����ͨ���޸�ϵͳ�����ļ����ɺ��ԣ�
// 1������socket ������socket����
// 2���󶨵�ַ
// 3����ʼ����
// 4����ʼepoll�¼�

int g_running = 1;
int backlog = 1024;
int max_fd = 1024;
#define MAX_MSG_LEN 1024 * 10
char read_buff[MAX_MSG_LEN] = {0};
char write_buff[MAX_MSG_LEN] = {0};
int read_offset = 0;
int write_offset = 0;
int serial = 0;

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

int read_cb(int epoll_fd, int fd)
{
	// �ȴӻ������ж�������
	// ��������Լ��Ͽ�����
	// �����ݰ�����ҵ���ȥ����ְ���ճ��������
	if (read_offset < 0 ||
		  read_offset >= MAX_MSG_LEN)
	{
		fprintf(stderr, "read buffer overflow\n");
		return -1;
	}
	
	int len = read(fd, read_buff + read_offset, MAX_MSG_LEN - read_offset);
	if (len < 0)
	{
		// Ҳ�п����������ݿɶ�
		if (errno == EAGAIN ||
			  errno == EINTR)
		{
			return 0;
		}
		else
		{
			perror("read error");
			// ֪ͨҵ��㣬�Ͽ�����
			printf("disconnection\n");
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		}		
	}
	else if (len == 0)
	{
		// �Ͽ�����		
		// ���ݵ�ҵ��㴦������߼�
		printf("disconnection\n");
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	}
	else
	{
		// ���ݴ��ݵ�ҵ��㣬�������º��ٻָ�������
		read_offset += len;
		printf("recv data, len=%d, data=%s\n", len, read_buff);
		// �ٰѽ��յ������ݣ�����һ�£������ͻ���
		serial++;
		char buff[MAX_MSG_LEN] = {0};
		sprintf(buff, "serial=[%d], data=%s", serial, read_buff);
		memcpy(write_buff + write_offset, buff, strlen(buff));
		write_offset += strlen(buff);
		// �޸ĳɿ�д
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLOUT;
		ev.data.fd = fd;
		epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
		// �߼��㴦����ϣ��ָ�������
		read_offset -= len;
	}
	return len;
}

int write_cb(int epoll_fd, int fd)
{
	if (write_offset < 0 ||
		  write_offset >= MAX_MSG_LEN)
	{
		fprintf(stderr, "write buffer overflow\n");
		return -1;
	}
	
	if (write_offset == 0)
	{
		return 0;
	}
	// ҵ����Ѿ�����д��buff��
	// �ȴ�epoll����д����
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
			perror("write error");
			// ֪ͨҵ��� �Ͽ�����
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		}		
	}
	else if (len == 0)
	{
		// ֪ͨҵ��� �Ͽ����ӣ���
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	}
	else
	{
		memmove(write_buff, write_buff+len, write_offset - len);
		write_offset -= len;
		printf("send data\n");
	}
}
int epoll_work()
{
	return 0;
}


int main(int argc, char** argv)
{
	struct sockaddr_in server_addr;
	int ret, listenfd, epoll_fd, nfds, connfd, curfds = 0;
	struct epoll_event ev;	
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr("192.168.17.101");
	server_addr.sin_port = htons(6666);
	
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0)
	{
		perror("create socket error");
		return -1;
	}
	
	// ���ÿ��Ը���
	// Ŀ���Ƿ��������º󣬸ö˿ڿ��Կ��ٽ��м���
	// ����Ҫ�ȴ�2��������
	int opt = 1;
	ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0)
	{
		perror("setsockopt error");
		return -1;
	}
	
	// ���÷�����
	ret = setnonblocking(listenfd);
	if (ret < 0)
	{
		perror("setnonblocking error");
		return -1;
	}
	
	// ��IP��ַ
	ret = bind(listenfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr));
	if (ret < 0)
	{
		perror("bind error");
		return -1;
	}
	
	ret = listen(listenfd, backlog);
	if (ret < 0)
	{
		perror("listen error");
		return -1;
	}
	
	epoll_fd = epoll_create(max_fd);
	ev.events = EPOLLIN;
	ev.data.fd = listenfd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listenfd, &ev);
	if (ret < 0)
	{
		fprintf(stderr, "epoll add fd=%d error\n", listenfd);
		return -1;
	}
	
	signal(SIGTERM, exit_signal);
	// ʹ��LTģʽ��������ETģʽ
	// LTģʽ�ĺô��������ݾͷ���
	// ����ET��Ҫ���ڲ�Ƕ��һ��ѭ��
	// Ҫһֱ������ȫ��ȡ��������
	// ȷ��ȫ�����ͳ�ȥ
	while(g_running)
	{
		struct epoll_event events[1024];
		nfds = epoll_wait(epoll_fd, events, max_fd, -1);
		if (nfds < 0)
		{
			perror("epoll wait error");
			return -1;
		}
		else if (nfds == 0)
		{
			usleep(100);
		}
		
		int i = 0;
		for (i = 0; i < nfds; i++)
		{
			if (events[i].data.fd == listenfd)
			{
				struct sockaddr_in client_addr;
				socklen_t socklen = sizeof(struct sockaddr_in);
				connfd = accept(listenfd, (struct sockaddr*)&client_addr, &socklen);
				if (connfd < 0)
				{
					if (errno == EAGAIN ||
					errno == EINTR)
					{
					}
					else
					{
						perror("accept error");
					}					
					continue;
				}
				
				printf("accept from[%s]\n", inet_ntoa(client_addr.sin_addr));
				if (curfds >= max_fd)
				{
					fprintf(stderr, "too many connections\n");
					close(connfd);					
					continue;
				}
				curfds++;
				// Ϊ�µ�������ӵ�events
				setnonblocking(connfd);				
				ev.events = EPOLLIN;
				ev.data.fd = connfd;
				ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd, &ev);
				if (ret < 0)
				{
					fprintf(stderr, "epoll add fd=%d error\n", connfd);
					return -1;
				}
				
				continue;
			}
			if (events[i].events & EPOLLIN)
			{
				read_cb(epoll_fd, events[i].data.fd);
			}
			if (events[i].events & EPOLLOUT)
			{
				write_cb(epoll_fd, events[i].data.fd);
			}
		}
	}	
}