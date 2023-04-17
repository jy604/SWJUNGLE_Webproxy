#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void read_requesthdrs(rio_t *rp); // 클라이언트로부터 수신한 HTTP 요청 헤더를 읽음
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
char *longmsg); // 클라이언트 에러 처리, HTTP 에러 응답 생성, 해당 응답을 클라이언트에게 전송함

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);
  int listenfd, connfd; // 듣기 소켓, 연결 소켓 초기화
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) { // port 입력 안되면 에러
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
// ./webserver 8080 -> argv[0] : ./webserver argv[1] : 8080
  listenfd = Open_listenfd(argv[1]); // 인자로 port 번호를 받아 듣기 소켓 생성함
  while (1) {
    clientlen = sizeof(clientaddr); // 클라이언트의 소켓 주소를 저장할 구조체의 크기 계산
    connfd = Accept(listenfd, (SA *)&clientaddr,&clientlen);  
    // line:netp:tiny:accept 클라이언트의 연결 요청을 수락, 통신할 소켓 생성
    // &clientaddr <- 클라이언트의 소켓 주소를 저장할 포인터 전달
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0); // 클라이언트의 ip주소와 포트번호 추출
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close 소켓 닫기
  }
}


void doit(int fd) {
  int serverfd;
  char *srcp, *lengptr, filename[MAXLINE], cgiargs[MAXLINE];
  struct stat sbuf; // 요청 받은 파일의 상태 정보 담는 구조체 선언
  char request_buf[MAXLINE], response_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  rio_t request_rio, response_rio;
 
  if ((serverfd = Open_clientfd("44.202.245.219", "8000")) < 0) {
    printf("proxy 연결안됨\n");
    return;
  }
  printf("proxy 연결됨\n");

  // client >>>>>>>>>>>>> proxy 클라이언트의 요청을 수신함 : 요청 라인 읽기
  Rio_readinitb(&request_rio, fd); // 클라이언트로부터 받은 데이터를 처리할 준비함
  Rio_readlineb(&request_rio, request_buf, MAXLINE); // 클라이언트로부터 받은 HTTP요청을 버퍼에 저장
  printf("--------FROM CLIENT----------\n");
  printf("Request line::: %s", request_buf);
  sscanf(request_buf, "%s %s %s", method, uri, version); //Parsing
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  //read_requesthdrs(&rio); // 아니라면 값을 받아들이고, 다른 요청 헤더를 무시

  printf("Request headers:::\n"); //요청 헤더 출력
  Rio_readlineb(&request_rio, request_buf, MAXLINE); // rp에서 최대 maxline만큼 바이트를 읽어 buf에 저장
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  while (strcmp(request_buf, "\r\n")) { // \r\n (CRLF)가 아닐때까지 반복 CRLF : 캐리지리턴, 라인피드
    Rio_readlineb(&request_rio, request_buf, MAXLINE); // \r\n가 나올때까지 line을 읽으면서 복사
    Rio_writen(serverfd, request_buf, strlen(request_buf));
    printf("%s", request_buf); // buf에 저장된 데이터 화면에 출력
  }

  // server >>>>>>>>>>>>> proxy : 서버의 응답을 받음 : 응답 라인 읽기
  Rio_readinitb(&response_rio, serverfd); // 응답 버퍼 초기화
  Rio_readlineb(&response_rio, response_buf, MAXLINE); // 라인 한 줄 읽기
  Rio_writen(fd, response_buf, strlen(response_buf)); // 버퍼에 담은 라인 클라이언트(fd)한테 보내기
  printf("--------TO CLIENT----------\n");
  printf("Response line::: %s", response_buf);

// 헤더 안에서 body size 가져오기
  printf("Response headers:::\n"); //응답 헤더 출력

  int body_size;
  while (strcmp(response_buf, "\r\n")) {
    Rio_readlineb(&response_rio, response_buf, MAXLINE); // \r\n가 나올때까지 line을 읽으면서 복사
    Rio_writen(fd, response_buf, strlen(response_buf));
    if (strstr(response_buf, "Content-length") != 0) { //content=length가 버퍼 안에 있으면
    // 문자열 중에 숫자만 빼야함 Content-length: 183
      lengptr = strchr(response_buf, ':');
      body_size = atoi(lengptr + 1);
      printf("body_size : %d\n",body_size);
    }
    //
    printf("%s", response_buf); // buf에 저장된 데이터 화면에 출력
  }

  if (body_size) {
    srcp = malloc(body_size);
    Rio_readnb(&response_rio, srcp, body_size);
    Rio_writen(fd, srcp, body_size);
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
  char *hostptr = strstr(uri, "//"); 
  char *portptr = strchr(hostptr, ':');
  // if (!strstr(uri, "cgi-bin")) { /* Static content - 정적 !(0) */
  //   strcpy(cgiargs, ""); // cgiargs에 빈문자열 복사 = 없애버림
  //   strcpy(filename, "."); // filename에 . 복사
  //   strcat(filename, uri); // .뒤에 uril 붙여 넣음 .'/index.html'를 만들기 위함
  //   if (uri[strlen(uri) - 1] == '/') // 만일 문자가 '/'로 끝난다면
  //       strcat(filename, "home.html"); //기본 파일 이름을 추가함
  //   return 1;
  // }
  // else { /* Dynamic content - 동적 */
  //   ptr = index(uri, '?'); // ?는 인자 구분하는 것, uri에 있는 ?의 위치부터 모든 CGI 인자를 추출 
  //   if (ptr) { // ? 가 존재하면
  //       strcpy(cgiargs, ptr + 1); // cgiargs 값에 ? 그 다음 값(ptr+1)을 넣어줌
  //       *ptr = '\0'; // 해당 ptr은 NULL로 변경
  //   }
  //   else { // ?가 없다면
  //       strcpy(cgiargs, ""); //cgiargs가 없음 > cgiargs를 지움 
  //   strcpy(filename, "."); // filename을 .으로 바꾸고
  //   strcat(filename, uri); // .뒤에 uri 붙여서 형태를 만들어줌
  //   return 0;
  //   }
  // }
}