//
// Created by vj-zhou on 2021/12/29.
//

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/wait.h>

#define IS_SPACE(x) isspace((int)(x))

#define STDERR  2
#define STDOUT  1
#define STDIN   0

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(void *);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void error_die(const char*);
void execute_cgi(int, const char*, const char*, const char*);
int get_line(int, char*, int);
void headers(int, const char*);
void not_found(int);
void serve_file(int, const char*);
int startup(u_short*);
void unimplemented(int);

/*
 * http 请求格式
 * 1. 请求方法URI 协议  : GET /xxx.html HTTP/1.1
 * 2. 请求头
 * 3. 请求正文
 */
void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    char method[256];
    char url[256];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;

    char* query_string = NULL;
    numchars = get_line(client, buf, sizeof(buf)); // 读取一行
    i = 0, j = 0;


    while(!IS_SPACE(buf[i]) && (i < sizeof(method) -1)) { // 获取请求方法
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) { // strcasecmp 比较字符串是否相等,相等返回0 忽略大小写
        unimplemented(client); // 没有实现GET || POST 之外的方法
        return;
    }

    if (strcasecmp(method, "POST") == 0) {
        cgi = 1;
    }

    i = 0;
    while(IS_SPACE(buf[j]) && (j < numchars)) j++; // 跳过空格

    while (!IS_SPACE(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) { // 写入uri
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0) { //  strcasecmp   string.h
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) { // GET http://xxxx.com/assets/js/fast.js?v=1640767556 HTTP/1.1 跳过? 之前的字符
            query_string++;
        }

        if (*query_string == '?') {
            cgi = 1;
            *query_string = '\0'; // http://xxxx.com/assets/js/fast.js\0v=1640767556 把? 变成\0, 截断url 字符串 => url = http://xxxx.com/assets/js/fast.js\o
            query_string++;
        }
    }
    sprintf(path, "htdocs%s", url); // path = "htdocshttp://xxxx.com/assets/js/fast.js\0"
    if (path[strlen(path)-1] == '/') { // 判断url 最后一个字符串是不是/
        strcat(path, "index.html"); // strcat 连接(concatenate)两个字符串    string.h
    }

    if (stat(path, &st) == -1) { // stat 通过path获取文件信息, success 0 failed -1 , 信息保存在st 结构体中
        while ((numchars > 0) && strcmp("\n", buf)) { // 将client缓冲区剩下的内容全部读完
            numchars = get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    } else {
        // 判断path 是不是目录
        if ((st.st_mode & S_IFMT) == S_IFDIR) { // st.st_mode 文件的类型和存取的权限  {S_IFMT   0170000    文件类型的位遮罩}  {S_IFDIR 0040000     目录}
            strcat(path, "/index.html"); // 拼接
        }
        //  S_IXUSR(S_IEXEC) 00100  文件所有者具可执行权限 | S_IXGRP 00010   用户组具可执行权限 | S_IXOTH 00001   其他用户具可执行权限
        // 检查权限, path 是一个可执行文件
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {
            cgi = 1;
        }

        if (cgi != 1) {
            serve_file(client, path);
        } else {
            execute_cgi(client, path, method, query_string);
        }
    }
    close(client);
}

int get_line(int sock, char* buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0); // 复制一个字符到c, 返回复制的字节数
        if (n > 0) { // 有接收到数据
            if (c == '\r') { // \r 光标移动到本行的开始的位置
                n = recv(sock, &c, 1, MSG_PEEK); // MSG_PEEK 从receive queue的开头返回数据, 而不会移除这个这个queue中数据
                if ((n > 0) && (c == '\n')) {
                    recv(sock, &c, 1, 0);
                } else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        } else {
            c = '\n';
        }
    }
    buf[i] = '\0';
    return i;
}

void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0); // 把buf 的数据放入client 剩余的缓冲区中
    sprintf(buf, SERVER_STRING); //把SERVER_STRING 写入buf
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</P></BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void not_found(int client)
{
    char buf[1024];

    printf(buf, "HTTP/1.0 404 NOT FOUND\\r\\n");
    send(client, buf, strlen(buf), 0); // 把buf 的数据放入client 剩余的缓冲区中
    sprintf(buf, SERVER_STRING); //把SERVER_STRING 写入buf
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Not Found\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void serve_file(int client,  const char* path)
{
    FILE *resource = NULL;

    int numchars = 1;
    char buf[1024];
    buf[0] = 'A'; buf[1] = '\0';

    while ((numchars > 0) && strcmp("\n", buf)) { // 将缓冲区内的数据读完并忽略
        numchars = get_line(client, buf, sizeof(buf));
    }

    resource = fopen(path, "r"); // 打开文件
    if (resource == NULL) {
        not_found(client);
    } else {
        headers(client, path); // 组装返回请求头信息
        cat(client, resource); // 读取文件内容
    }
    fclose(resource);
}
// Return the informational HTTP Header about file
void headers(int client, const char* path)
{
    char buf[1024];
    (void)path; // could use path to determine file type

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE* fp) {
    char buf[1024];

    fgets(buf, sizeof(buf), fp);
    while (!feof(fp)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), fp);
    }
}

void execute_cgi(int client, const char* path, const char* method, const char* query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0'; // 确保buf 中有数据

    if (strcasecmp(method, "GET") == 0) { // GET 请求将缓存内的数据读取完并忽略
        while ((numchars > 0) && strcmp("\n", buf)) {
            numchars = get_line(client, buf, sizeof(buf));
        }
    } else if (strcasecmp(method, "POST") == 0) { // 读取缓存中全部的数据获取Content-Length对应的body长度
        numchars = get_line(client, buf, sizeof(buf));
        while( (numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0'; //  Content-Length: 100.  Content-Length:长度是15
            if (strcasecmp(buf, "Content-Length:") == 0) {
                content_length = atoi(&buf[16]); // buf[15] = '\0'后, buf 被隔断了 &buf[16] = buf
            }
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
        }
    } else {

    }
    // 两个管道, 用于进程间通信, 只能用于父子进程/兄弟进程或者具有亲缘关系的进程
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return ;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return ;
    }

    if ((pid = fork()) < 0) { // 创建子进程
        cannot_execute(client);
        return ;
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if (pid == 0) { // 子进程
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //  cgi_output|cgi_input  0 读管道 1 写管道
        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env); // 添加环境变量
        } else {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    } else { // parent
        close(cgi_input[0]);
        close(cgi_output[1]);
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, i);
            }
        }
        while (`````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````read`````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````````(cgi_output[0], &c, 1) > 0) {
            send(client, &c, 1, 0);
        }
        close(cgi_input[1]);
        close(cgi_output[0]);
        waitpid(pid, &status, 0); // 监控子进程
    }
}

void bad_request(int client)
{
    char buf[1024];
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

void cannot_execute(int client)
{
    char buf[1024];
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

void error_die(const char* sc) {
    perror(sc);
    exit(1);
}

int startup(u_short* port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1) {
        error_die("socket");
    }

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        error_die("setsockopt failed");
    }

    if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) == -1) {
        error_die("bind");
    }

    if (*port == 0) {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1) {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }

    if (listen(httpd, 5) == -1) {
        error_die("listen");
    }

    return httpd;
}

int main ()
{
    int serv_sock = -1;
    u_short port = 9991;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);

    pthread_t tid;

    serv_sock = startup(&port);
    printf("httpd running on %d\n", port);

    while (1) {
        client_sock = accept(serv_sock, (struct sockaddr*)&client_name, &client_name_len);
        if (client_sock == -1) {
            error_die("accept");
        }

        if (pthread_create(&tid, NULL, (void*)accept_request, (void*)(intptr_t)client_sock) != 0) {
            perror("pthread_create");
        }
    }
    close(serv_sock);
    return 0;
}