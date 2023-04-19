#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct web_object {
  char *response_ptr; // 버퍼의 주소
  struct web_object *prev;
  struct web_object *next;
  int content_length;
  char path[MAXLINE];
} web_object_t;

web_object_t *root_ptr; // 캐시 리스트의 루트
web_object_t *last_ptr;
int total_cache_size = 0;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// 함수 원형 선언
void doit(int fd);
void read_requesthdrs(rio_t *rp); // 클라이언트로부터 수신한 HTTP 요청 헤더를 읽음
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);
web_object_t *find_cache(char *path);
void send_cache(web_object_t *web_object, int fd);
void read_cache(web_object_t *web_object);
void write_cache(web_object_t *web_object);



int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);
  int listenfd, *connfd; // 듣기 소켓, 연결 소켓 초기화
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) { // port 입력 안되면 에러
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  root_ptr = (web_object_t *)calloc(1, sizeof(web_object_t));
  listenfd = Open_listenfd(argv[1]); // 인자로 port 번호를 받아 듣기 소켓 생성함
  while (1) {
    clientlen = sizeof(clientaddr); // 클라이언트의 소켓 주소를 저장할 구조체의 크기 계산
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0); // 클라이언트의 ip주소와 포트번호 추출
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd);
  }
}

void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self()); // 명시적 반환 x, 연결 종료시 자동으로 메모리 반환
  Free(vargp); // main에서 malloc한 부분을 free
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd) {
  int serverfd;
  char *srcp, *lengptr, filename[MAXLINE], cgiargs[MAXLINE];
  struct stat sbuf; // 요청 받은 파일의 상태 정보 담는 구조체 선언
  char request_buf[MAXLINE], response_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE]; 
  rio_t request_rio, response_rio;


  // 요청 라인 읽기 client >>>>>>>>>>>>> proxy 클라이언트의 요청을 수신함
  Rio_readinitb(&request_rio, fd); // 클라이언트로부터 받은 데이터를 처리할 준비함
  Rio_readlineb(&request_rio, request_buf, MAXLINE); // 클라이언트로부터 받은 HTTP요청을 버퍼에 저장
  printf("--------FROM CLIENT----------\n");
  printf("Request line::: %s", request_buf);
  sscanf(request_buf, "%s %s %s", method, uri, version); //Parsing
  parse_uri(uri, hostname, port, path);
  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
  printf("host ::::::::::::::: HOSTNAME: %s PORT: %s\n", hostname, port);
  printf("");

  web_object_t *cur_web_object = find_cache(path);
  if (cur_web_object != NULL) {// if 구조체 주소 있다면:
    send_cache(cur_web_object, fd); // 캐시 안에 데이터 읽어서 보내기
    read_cache(cur_web_object); // 읽은 캐시를 갱신
    return;
  }

  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // server 소켓 생성
  // if ((serverfd = Open_clientfd("44.202.245.219", "8000")) < 0) { // 브라우저 테스트용
  if ((serverfd = Open_clientfd(hostname, port)) < 0) {
    printf("proxy 연결안됨\n");
    return;
  }
  printf("proxy 연결됨\n");

  //요청 라인 전송 proxy >>>>>>>>>> server
  Rio_writen(serverfd, request_buf, strlen(request_buf));

// 요청 헤더 읽고, 출력
// printf("Request headers:::\n"); // 구현 x
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  printf("Request headers:::\n");
  while (strcmp(request_buf, "\r\n")) { 
    Rio_readlineb(&request_rio, request_buf, MAXLINE); 
    Rio_writen(serverfd, request_buf, strlen(request_buf));
    printf("%s", request_buf); // buf에 저장된 데이터 화면에 출력
  }

  // 응답 라인 읽기 : server >>>>>>>>>>>>> proxy
  Rio_readinitb(&response_rio, serverfd); // 응답 버퍼 초기화
  Rio_readlineb(&response_rio, response_buf, MAXLINE); // 라인 한 줄 읽기
  Rio_writen(fd, response_buf, strlen(response_buf)); // server >>>>> client 응답 라인 전송
  printf("--------TO CLIENT----------\n");
  printf("Response line::: %s", response_buf);

// 응답 헤더 읽기 : server >>>>>>> proxy
  printf("Response headers:::\n");
  int content_length;
  while (strcmp(response_buf, "\r\n")) 
  {
    Rio_readlineb(&response_rio, response_buf, MAXLINE);
    Rio_writen(fd, response_buf, strlen(response_buf)); // server >>>>> client 응답 헤더 전송
    if (strstr(response_buf, "Content-length") != 0) { //content=length가 버퍼 안에 있으면
    // 문자열 중에 숫자만 빼야함 Content-length: 183
      lengptr = strchr(response_buf, ':');
      content_length = atoi(lengptr + 1);
      printf("바디 길이 : %d\n",content_length);
    }
    printf("%s", response_buf); // buf에 저장된 데이터 화면에 출력
  }

// 응답 바디 읽기 : server >>>>>>>>> proxy
    srcp = malloc(content_length);
    Rio_readnb(&response_rio, srcp, content_length);
    if (content_length <= MAX_OBJECT_SIZE) 
    {
      web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
      web_object->response_ptr = srcp;
      web_object->content_length = content_length;
      strcpy(web_object->path, path); // path 복사
      write_cache(web_object);
      Rio_writen(fd, srcp, content_length); // 응답 바디 전송 : proxy >>>>> client
    } 
    else 
    {
      Rio_writen(fd, srcp, content_length);
      free(srcp);
    }
}

// 모든 실제 에러 처리 x, 명백한 에러는 체크함 
// 오류 응답에 필요한 정보를 받아들이고, 해당 오류에 대한 HTTP 응답 메시지를 생성하여 클라이언트에게 전송
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body - HTTP 응답 메시지 본체(body) 구성하는 부분 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  // buf : HTTP응답 메시지의 헤더를 구성
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 상태 라인 : errnum : 상태 코드, shortmsg : 상태 메시지
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n"); //HTTP헤더 
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  // HTTP 응답 메시지를 클라이언트에게 보냄
  Rio_writen(fd, buf, strlen(buf)); // 전송할 문자열의 길이를 함께 지정(strlen)하여 버퍼 오버런 방지
  Rio_writen(fd, body, strlen(body));
}
// tiny는 요청 헤더 내의 어떠한 정보도 사용하지 않음 = 단순성, 안전성때문 > 헤더 읽고 걍 무시함

// modify 적용 x, 사용 안함
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE); // rp에서 최대 maxline만큼 바이트를 읽어 buf에 저장
  while (strcmp(buf, "\r\n")) { // \r\n (CRLF)가 아닐때까지 반복 CRLF : 캐리지리턴, 라인피드
    Rio_readlineb(rp, buf, MAXLINE); // \r\n가 나올때까지 line을 읽으면서 복사
    printf("%s", buf); // buf에 저장된 데이터 화면에 출력
  }
  return;
}

// uri 형태: `http://hostname:port/path` 혹은 `http://hostname/path` (port는 optional)
int parse_uri(char *uri, char *hostname, char *port, char *path) {
  char *host_ptr = strstr(uri, "//") != NULL ? strstr(uri, "//") + 2 : uri; // http:// 자름 hostname:port/path
  char *port_ptr = strchr(host_ptr, ':'); // port 시작 위치 port/path
  char *path_ptr = strchr(host_ptr, '/'); // path 시작 위치 port/path

  if (port_ptr != NULL) {// 포트가 있는 경우
    strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
    port[path_ptr - port_ptr - 1] = '\0';
    strncpy(hostname, host_ptr, port_ptr - host_ptr); // hostname 구하기
  }
  else {   // 포트가 없는 경우
    strcpy(port, "80");                                               
  strncpy(hostname, host_ptr, path_ptr - host_ptr);
  }
  strcpy(path, path_ptr);
  return;
}

web_object_t *find_cache(char *path) {
  if (root_ptr -> content_length == 0) {/// 캐시에 들어있는 게 없으면
    return NULL;
  }
  // curr_ptr의 path와 path가 다를때까지 curr_ptr을 next로 옮기며 탐색
  for(web_object_t *curr_ptr = root_ptr; strcmp(curr_ptr->path, path); curr_ptr = curr_ptr->next) {
    if(curr_ptr->next->content_length == 0) { // 다음 노드가 없으면
      return NULL;
    }
    if(strcmp(curr_ptr->next->path, path) == 0) { // 현재 들어온 path값이 캐시 안에 있으면
      return curr_ptr->next;
    }
  }
  return NULL;
}

// 캐시에 find_cache로 찾은 헤더의 정보가 담겨져있다면 해당 캐시를 바로 클라이언트에게 보내는 함수
void send_cache(web_object_t *web_object, int fd) {
// 응답 헤더 만들기
// connfd에게 헤더 보내기
// connfd에게 cache 내용 보내기
rio_t rio;
char buf[MAXLINE];
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 상태 라인 : errnum : 상태 코드, shortmsg : 상태 메시지
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);// 연결 방식
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, web_object->content_length); // 컨텐츠 길이
  Rio_writen(fd, buf, strlen(buf));
  // HTTP 응답 메시지를 클라이언트에게 보냄
  Rio_writen(fd, web_object->response_ptr, web_object->content_length);
}

// 캐시 읽고, 쓰는 함수 (리스트 재배치)
void read_cache(web_object_t *web_object) {
// A > B > C
// case 1: 캐시를 읽는데, 이미 그 전에 읽은 캐시가 현재 읽을 캐시일 경우 : 그냥 반환
if (web_object == root_ptr) { 
  return;
}
// case 2: 현재 읽을 캐시가 B의 위치에 있고, 전, 후에 캐시가 다 있을 경우
if (web_object->next->content_length != 0) {
    web_object->prev->next = web_object->next; //현재 노드의 next가 현재 노드의 prev가 됨
    web_object->next->prev = web_object->prev; //현재 노드의 prev가 현재 노드의 next가 됨
    web_object->next = root_ptr; //루트 노드 바꾸기
    root_ptr = web_object;
}
  // case3: 현재 노드가 마지막 노드일 경우
else { 
    web_object->prev->next = NULL; // 현재 노드의 앞(prev) 노드의 next가 null
    web_object->next = root_ptr;
    root_ptr = web_object;
  }
}

void write_cache(web_object_t *web_object) {
  // 캐시 메모리 최대 크기보다 넘치면 가장 오래된 캐시 삭제
  if ((web_object->content_length + total_cache_size) > MAX_CACHE_SIZE) {
    int tmp = last_ptr->content_length;
    last_ptr = last_ptr-> prev;
    free(last_ptr->next);
    last_ptr->next = NULL;
    total_cache_size -= tmp;
  }
  if (root_ptr != NULL) {
    web_object->next = root_ptr;
    root_ptr->prev = web_object;
  }
  root_ptr = web_object;
  total_cache_size += web_object->content_length;
}