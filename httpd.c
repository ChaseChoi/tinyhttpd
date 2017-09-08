/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
// #include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x)) //判断是否为空格

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(int client)
{
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
 char *query_string = NULL;

 numchars = get_line(client, buf, sizeof(buf)); //获取一行数据，结束符号为\n，返回字符的数量
 i = 0; j = 0;
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))  //提取出方法method
 {
  method[i] = buf[j]; //读取client的initial request line(method URL protocol)
  i++; j++;
 }
 method[i] = '\0'; //设置结束符号

 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))  //既不是GET也不是POST，提示错误，并返回
 {
  unimplemented(client); //回复客户端501错误
  return;
 }

 if (strcasecmp(method, "POST") == 0) 
  cgi = 1; //设置cgi为0,开启cgi

 i = 0;
 while (ISspace(buf[j]) && (j < sizeof(buf))) 
  j++;
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) //提取URL 
 {
  url[i] = buf[j];
  i++; j++;
 }
 url[i] = '\0'; //设置字符串结束符

 if (strcasecmp(method, "GET") == 0) //如果method为GET
 {
  query_string = url; 
  while ((*query_string != '?') && (*query_string != '\0')) //寻找到?或者\0
   query_string++;
  if (*query_string == '?')  //URL中含有query
  {
   cgi = 1; //开启cgi
   *query_string = '\0'; //分离query
   query_string++;  
  }
 }
                                  //URL已经除去了query
 sprintf(path, "htdocs%s", url); //HyperText Documents用于存放供client读取的文件，如index.html,用户可通过域名访问
 if (path[strlen(path) - 1] == '/') 
  strcat(path, "index.html"); //连接上"index.html",构造路径
 if (stat(path, &st) == -1) { //文件状态获取失败
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
  not_found(client); //返回404错误
 }
 else
 {                                       //“&”按位与，通过二进制数比较可以知道对应mode
  if ((st.st_mode & S_IFMT) == S_IFDIR) //S_IFDIR(宏定义)表示目录,判断是否为目录
   strcat(path, "/index.html");       //如果是目录，路径中增加对应文件
  if ((st.st_mode & S_IXUSR) ||     //文件所有者具可执行权限
      (st.st_mode & S_IXGRP) ||     //用户组具可执行权限
      (st.st_mode & S_IXOTH)    )   //其他用户具可执行权限  
   cgi = 1;
  if (!cgi)  //不具备相应权限
   serve_file(client, path); //返回资源。失败提示客户端404，成功则提示相关信息
  else      //具备权限则执行cgi
   execute_cgi(client, path, method, query_string); 
 }

 close(client);  //释放描述符
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)  //提醒客户端请求发生问题
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");  //构造信息主体，提示400错误
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

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];
 
 fgets(buf, sizeof(buf), resource);  //资源写入buf中
 while (!feof(resource))   //判断是否到达文件尾
 {
  send(client, buf, strlen(buf), 0); //传送资源给客户端
  fgets(buf, sizeof(buf), resource);  
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client) //提示客户端500错误，CGI脚本无法正常运行
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n"); //构造回复信息主体，提示500错误
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc); //输出最近一次的错误信息
 exit(1); //退出
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,                //执行CGI脚本
                 const char *method, const char *query_string)
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

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)  //判断是否为GET方法
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf)); //除去headers
 else    /* POST */
 {
  numchars = get_line(client, buf, sizeof(buf)); 
  while ((numchars > 0) && strcmp("\n", buf))
  {
   buf[15] = '\0';
   if (strcasecmp(buf, "Content-Length:") == 0)
    content_length = atoi(&(buf[16])); //类型转换 字符串转为整型
    numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
   bad_request(client); //返回给客户端错误信息
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
 send(client, buf, strlen(buf), 0); //回复客户端成功提示

 if (pipe(cgi_output) < 0) {
  cannot_execute(client); //向客户端返回500错误信息
  return;
 }
 if (pipe(cgi_input) < 0) {
  cannot_execute(client); //向客户端返回500错误信息
  return;
 }

 if ( (pid = fork()) < 0 ) {  //fork()发生错误
  cannot_execute(client);  //向客户端返回500错误信息
  return;
 }
 if (pid == 0)  /* child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];
                          //cgi使用标准输入输出进行交互
  dup2(cgi_output[1], 1); //关闭标准输出,让标准输出进入cgi_output[1]写端
  dup2(cgi_input[0], 0);  //关闭标准输入,让标准输入进入cgi_input[0]读端
  close(cgi_output[0]); //关闭读端口
  close(cgi_input[1]);  //关闭写端口
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env); //修改环境变量
  if (strcasecmp(method, "GET") == 0) {
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env); //修改环境变量
  }
  else {   /* POST */
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);  //修改环境变量
  }
  execl(path, path, NULL); //执行path指示的文件
  exit(0);  //退出
 } else {    /* parent */
  close(cgi_output[1]); //关闭fd
  close(cgi_input[0]);
  if (strcasecmp(method, "POST") == 0)  //POST使用stdin获取输入，这里写入管道，POST从管道读取数据
   for (i = 0; i < content_length; i++) {
    recv(client, &c, 1, 0);  //读取内容
    write(cgi_input[1], &c, 1);
   }
  while (read(cgi_output[0], &c, 1) > 0)  //GET使用stdout输出数据，现在直接从管道读取
   send(client, &c, 1, 0);  

  close(cgi_output[0]);  //释放fd
  close(cgi_input[1]);   //释放fd
  waitpid(pid, &status, 0);  //等待子进程结束
 }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) //功能：换行符统一为\n
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0); //限定buf为1个char的空间
  /* DEBUG printf("%02X\n", c); */
  if (n > 0) //读取到了内容
  {
     if (c == '\r') //判断读取到的内容是否为\r
     {
      n = recv(sock, &c, 1, MSG_PEEK); //MSG_PEEK：从buffer读取，但不把读取的数据移除，再次读取还能够读到
      /* DEBUG printf("%02X\n", c); */
      if ((n > 0) && (c == '\n')) //读到\n
       recv(sock, &c, 1, 0); //读取\n到c
      else
       c = '\n';  
     } 
   buf[i] = c; //accept_request()传递的buf数组,非换行字符和\n存入其中
   i++;
  }  
  else //没读到内容
   c = '\n';
 } 
 buf[i] = '\0';
 
 return(i); //返回结束位置
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */
  //返回文件的相关信息给客户端，提示200信息
 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client) //返回客户端404错误
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n"); //状态行
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING); //header部分
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n"); //message body部分
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
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

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
 FILE *resource = NULL;
 int numchars = 1;
 char buf[1024];

 buf[0] = 'A'; buf[1] = '\0';
 while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
  numchars = get_line(client, buf, sizeof(buf));

 resource = fopen(filename, "r");
 if (resource == NULL) //资源不存在
  not_found(client);  //返回404 NOT FOUND
 else
 {
  headers(client, filename); //返回文件的相关信息给客户端
  cat(client, resource);  //发送客户端请求的资源
 }
 fclose(resource);  //关闭流
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0); //AF_INET也可以;面向连接的TCP;0参数系统会基于参数二自动选择合适的协议
 if (httpd == -1) //出错 
  error_die("socket"); //输出错误信息
 memset(&name, 0, sizeof(name)); // 置0初始化
 name.sin_family = AF_INET;  //IPv4
 name.sin_port = htons(*port); //host to network short 类型转换
 name.sin_addr.s_addr = htonl(INADDR_ANY);//host to network long 类型转换，作为服务器使用INADDR_ANY与所有接口bind
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) //fd与地址捆绑，为listen()准备，name记得执行类型转换
  error_die("bind"); //输出错误信息
 if (*port == 0)  /* if dynamically allocating a port */ //端口号为0,动态分配端口号
 {
  int namelen = sizeof(name); 
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1) 
   error_die("getsockname");
  *port = ntohs(name.sin_port); //network to host short类型转换 获取分配到的端口号
 }
 if (listen(httpd, 5) < 0)  //队列长度限定为5
  error_die("listen"); //输出错误信息
 return(httpd);  //返回socket
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client) //提示客户端该请求没有实现
{
 char buf[1024];
 //构造回复，提示客户端方法未实现
 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");  
 send(client, buf, strlen(buf), 0); 
 sprintf(buf, SERVER_STRING);
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
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
 int server_sock = -1;
 u_short port = 0; //服务器端口号
 int client_sock = -1;
 struct sockaddr_in client_name; //用于accept()存储“客户端”的地址信息 
 int client_name_len = sizeof(client_name); //在传递参数前必须设置为sizeof(client_name)
 // pthread_t newthread;

 server_sock = startup(&port); //执行初始化工作，获得socket fd
 printf("httpd running on port %d\n", port);

 while (1)
 {
  client_sock = accept(server_sock,                      //获取与客户端通信的socket文件描述符
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  if (client_sock == -1) //fd获取失败
   error_die("accept"); //输出错误信息
  accept_request(client_sock); 
 // if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
 //   perror("pthread_create");
 }

 close(server_sock); //释放socket文件描述符

 return(0);
}
