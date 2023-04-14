/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp); // 클라이언트로부터 수신한 HTTP 요청 헤더를 읽음
// 클라이언트가 요청한 URI에서 파일 경로와 CGI 인자 추출
// 동적 CGI를 요청한 경우 : 파일 경로와 CGI 인자 모두 추출
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize); // 정적 파일을 클라이언트로 전송
void get_filetype(char *filename, char *filetype); // 파일의 확장자를 기반으로 해당 파일의 MIME 타입 결정
void serve_dynamic(int fd, char *filename, char *cgiargs); // 동적 CGI 프로그램을 실행, 클라이언트로 결과 전송
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg); // 클라이언트 에러 처리, HTTP 에러 응답 생성, 해당 응답을 클라이언트에게 전송함

void doit(int fd) {
  int is_static; // 정적 콘텐츠인지 확인하는 변수
  struct stat sbuf; // 요청 받은 파일의 상태 정보 담는 구조체 선언
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  //MAXLINE이 8192인 이유 : MAXLINE은 버퍼의 크기를 나타내는 상수, HTTP요청 및 응답 헤더의 최대 크기가 대부분 8192 이하
  // 이를 넘으면 메모리 오버플로우가 발생함 
  char filename[MAXLINE], cgiargs[MAXLINE];
  // filename : 요청받은 파일의 이름을 저장하기 위한 버퍼
  // cgiargs : CGI프로그램의 인자를 저장하기 위한 버퍼
  rio_t rio; // 10장 참고

  Rio_readinitb(&rio, fd); // 클라이언트로부터 받은 데이터를 처리할 준비함
  Rio_readlineb(&rio, buf, MAXLINE); // 클라이언트로부터 받은 HTTP요청을 버퍼에 저장
  printf("Request headers:\n"); //요청 헤더 출력
  printf("%s", buf); // 버퍼 출력
  sscanf(buf, "%s %s %s", method, uri, version); //Parsing
  
  // buf : 파싱할 대상 문자열 공백으로 구분하여 3개(%s)의 문자열로 파싱-> method, uri, version에 파싱 결과 저장
  // buf = GET /index.html HTTP/1.1 method : "GET" / uri : "/index.html" version : "HTTP/1.1"
  // 메소드 에러 처리 : tiny는 GET method만 지원, 다른 method 요청하면 에러메시지(501) 보내고, main으로 돌아감
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // 아니라면 값을 받아들이고, 다른 요청 헤더를 무시

  // parse URI from GET request : uri를 파싱해서 파일 이름과 CGI인자를 추출, 
  // 요청이 정적, 동적인지 구분하는 플래그 설정
  is_static = parse_uri(uri, filename, cgiargs);
  // parse_uri를 통해서 인자가 있는지 확인하고 있으면 있는대로 없으면 없는대로 uri를 재정리해줌 > 정적 1 동적 0 return
  /* stat() : 파일의 메타정보를 검색하는 함수 sys/stat.h에 선언 filename 인자로 전달 > 파일의 정보를 구조체로 채워 포인터로 전달
  stat()는 파일 유형, 권한, 소유자, 수정 시간을 알려줌, 호출 성공시 0 / 실패 시 -1을 return errno변수에 오류 코드 저장됨*/
  
  // 파일 에러 처리 :-1 > 파일이 디스크상에 없다는 뜻 즉시 클라이언트에게 에러메시지 전송 후 return
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }

// 동적 정적 컨텐츠 에러 처리
  if (is_static) { /*Serve static content = request가 정적 컨텐츠라면*/
  // S_ISREG : 일반 파일인지 검증 || S_IRUSR : 읽기 권한 여부 검사
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // OR연산 : 둘다 0이어야 if문 진입
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // 정적 콘텐츠 클라이언트에게 제공
  }
  else { /* Serve dynamic content = request가 동적 컨텐츠라면*/
  // 일반 파일인지 검증 || S_IXUSR : 실행 권한 여부 검사
    if (!(S_ISREG(sbuf.st_mode)) || (!S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // 동적 컨텐츠 클라이언트에게 제공
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
// 정적 콘텐츠를 위한 홈 디렉토리는 자신의 현재 디렉토리, 동적 콘텐츠는 /cgi-bin 로 가정
// strstr(str1, str2) : str1 안에 str2가 있는지 확인, 있으면 1, 없으면 0
// strcpy(char* dest, const char* src) dest에 src 복사
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) { /* Static content - 정적 !(0) */
    strcpy(cgiargs, ""); // cgiargs에 빈문자열 복사 = 없애버림
    strcpy(filename, "."); // filename에 . 복사
    strcat(filename, uri); // .뒤에 uril 붙여 넣음 .'/index.html'를 만들기 위함
    if (uri[strlen(uri) - 1] == '/') // 만일 문자가 '/'로 끝난다면
        strcat(filename, "home.html"); //기본 파일 이름을 추가함
    return 1;
  }
  else { /* Dynamic content - 동적 */
    ptr = index(uri, '?'); // ?는 인자 구분하는 것, uri에 있는 ?의 위치부터 모든 CGI 인자를 추출 
    if (ptr) { // ? 가 존재하면
        strcpy(cgiargs, ptr + 1); // cgiargs 값에 ? 그 다음 값(ptr+1)을 넣어줌
        *ptr = '\0'; // 해당 ptr은 NULL로 변경
    }
    else { // ?가 없다면
        strcpy(cgiargs, ""); //cgiargs가 없음 > cgiargs를 지움 
    strcpy(filename, "."); // filename을 .으로 바꾸고
    strcat(filename, uri); // .뒤에 uri 붙여서 형태를 만들어줌
    return 0;
    }
  }
}

// 정적 컨텐츠에서 사용하는 적합한 file type을 찾는 함수
void serve_static(int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  // tiny는 5개의 서로 다른 정적 컨텐츠 타입 지원 : html, 무형식 텍스트, gif, png, jpeg으로 인코딩으로 된 영상

  /* Send response headers to client */
  get_filetype(filename, filetype); // 파일 접미어로 파일의 타입을 결정, http 응답을 보냄
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 클라이언트에게 RCLH(응답 줄, 헤더) 보냄
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); 
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); // \r\n이 반복 > 빈줄 하나 더 생김 > 헤더 종료
  Rio_writen(fd, buf, strlen(buf)); // 파일 내용 클라이언트에게 보냄
  printf("Response headers:\n");
  printf("%s", buf); // 마지막으로 재확인

  /* Send response body to client - 요청한 파일의 내용을 fd(연결식별자)로 복사해서 응답 본체를 보냄 */
  srcfd = Open(filename, O_RDONLY, 0); //filename open, 식별자 들고옴
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 가상메모리에 파일 내용 매핑
  Close(srcfd); // 메모리에 매핑한 후에는 식별자 필요 없음, 파일을 닫음 > 안 닫으면 누수
  Rio_writen(fd, srcp, filesize); // 실제로 파일을 클라이언트에게 전송
  //주소 srcp에서 시작하는 filesize 바이트를 클라이언트 연결 식별자로 복사
  Munmap(srcp, filesize); // 매핑된 가상 메모리 주소를 반환 > 메모리 누수를 위해 필수
}

  /*
  * get_filetype - Derive file type from filename
  */
 // 적합한 filetype를 찾는 함수
 void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) 
      strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
      strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
      strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
      strcpy(filetype, "image/jpeg");
  else
      strcpy(filetype, "text/plain");
 }
 
// setenv 함수 : 환경변수의 새 변수를 정의하거나 호출 > 
// 환경 변수 이름, 값, 이미 같은 이름이 있다면 값을 변경할지 여부확인 (0은 고정, 1은 변경)
/* Tiny는 child process를 fork 하고, 
CGI 프로그램을 자식의 context에서 실행하며, 
모든 종류의 동적 콘텐츠를 제공 */
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 성공 응답라인으로 시작
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* Child - 응답 첫번째 보낸 후 새로운 자식 fork */
  /* Real server would set all CGI vars here */

  setenv("QUERY_STRING", cgiargs, 1); // QUERY_STRING 환경 변수를 요청하면 uri의 cgi인자들이 초기화
  Dup2(fd, STDOUT_FILENO);      /* Redirect stdout to client - 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정 */
  /*
    dup2는 newfd 파일 descripter가 이미 open된 파일이면 close하고 oldfd를 newfd로 복사함
    parameter
    oldfd : 복사하려는 원본 file descripter
    newfd : 복사되는 target file descripter 
            (만약 newfd가 열려진 file descripter이면, 먼저 close후에 복사함)
  */
  Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child 자식 종료를 기다리기 위해 wait */
}

// tiny = 반복실행 서버, 서버가 무한루프로 대기(listen)탐.
// 반복적으로 연결 요청을 접수하고(accept), 트랜잭션 수행(doit), 자신쪽의 연결 끝을 닫음(close)
int main(int argc, char **argv) {
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

    //test
  }
}