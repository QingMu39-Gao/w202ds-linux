/* SPDX-License-Identifier: GPL-2.0-only */
/* Small shared POSIX helpers. No hardware or desktop policy lives here. */
#ifndef W202_NATIVE_H
#define W202_NATIVE_H
#define _GNU_SOURCE
#include <ctype.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static char *read_text(const char *path) {
    FILE *f=fopen(path,"re"); if(!f) return NULL;
    char *s=NULL; size_t n=0; ssize_t k=getline(&s,&n,f); fclose(f);
    if(k<0){free(s);return NULL;} while(k>0 && isspace((unsigned char)s[k-1]))s[--k]=0;
    return s;
}
static int write_text(const char *path,const char *value) {
    int fd=open(path,O_WRONLY|O_CLOEXEC|O_TRUNC); if(fd<0)return -1;
    size_t n=strlen(value); ssize_t k=write(fd,value,n); int saved=errno;
    close(fd);errno=saved;return k==(ssize_t)n?0:-1;
}
static bool exists(const char *path){struct stat s;return !stat(path,&s);}
static char *path_join(const char *a,const char *b){char *p=NULL;if(asprintf(&p,"%s/%s",a,b)<0)abort();return p;}
static char *selected(const char *s){if(!s)return strdup("");const char *a=strchr(s,'['),*b=a?strchr(a,']'):NULL;return b?strndup(a+1,b-a-1):strdup(s);}
static void json_string(FILE *out,const char *s) {
    if(!s){fputs("null",out);return;}fputc('"',out);
    for(const unsigned char *p=(const unsigned char*)s;*p;p++){
        if(*p=='"'||*p=='\\'){fputc('\\',out);fputc(*p,out);}
        else if(*p<32)fprintf(out,"\\u%04x",*p);else fputc(*p,out);
    }fputc('"',out);
}
static int json_error(const char *s){fputs("{\"ok\":false,\"error\":",stdout);json_string(stdout,s);puts("}");return 1;}
/* Strict bounded JSON tokenizer. Tokens keep original slices for lossless relay. */
struct token { size_t start,end; int next; char type; };
struct json {const char *s;size_t pos;struct token *v;int n,cap;bool bad;};
static void jspace(struct json *j){while(isspace((unsigned char)j->s[j->pos]))j->pos++;}
static int jvalue(struct json *j,int depth){
    jspace(j);if(depth>32||j->n>=32768||!j->s[j->pos]){j->bad=true;return -1;}
    if(j->n==j->cap){j->cap=j->cap?j->cap*2:32;j->v=realloc(j->v,j->cap*sizeof(*j->v));if(!j->v)abort();}
    int t=j->n++;j->v[t]=(struct token){.start=j->pos,.type=j->s[j->pos]};char c=j->s[j->pos++];
    if(c=='{'||c=='['){
        char close=c=='{'?'}':']';jspace(j);
        if(j->s[j->pos]!=close)for(;;){
            jspace(j);if(c=='{'&&j->s[j->pos]!='"'){j->bad=true;break;}
            jvalue(j,depth+1);if(j->bad)break;
            if(c=='{'){jspace(j);if(j->s[j->pos++]!=':'){j->bad=true;break;}jvalue(j,depth+1);if(j->bad)break;}
            jspace(j);if(j->s[j->pos]!=',')break;j->pos++;
        }
        if(!j->bad){if(j->s[j->pos]!=close)j->bad=true;else j->pos++;}
    }else if(c=='"'){
        bool closed=false;while(j->s[j->pos]){unsigned char x=j->s[j->pos++];if(x=='"'){closed=true;break;}
            if(x<32){j->bad=true;break;}if(x=='\\'){char e=j->s[j->pos];if(!e){j->bad=true;break;}j->pos++;
                if(e=='u'){for(int i=0;i<4;i++){if(!isxdigit((unsigned char)j->s[j->pos])){j->bad=true;break;}j->pos++;}}
                else if(!strchr("\"\\/bfnrt",e))j->bad=true;
            }if(j->bad)break;
        }if(!closed)j->bad=true;
    }else if(c=='t'||c=='f'||c=='n'){
        const char *word=c=='t'?"true":c=='f'?"false":"null";size_t n=strlen(word);
        if(strncmp(j->s+j->pos-1,word,n))j->bad=true;else j->pos+=n-1;
    }else{
        j->pos--;size_t start=j->pos;if(j->s[j->pos]=='-')j->pos++;
        if(j->s[j->pos]=='0')j->pos++;
        else if(j->s[j->pos]>='1'&&j->s[j->pos]<='9'){while(isdigit((unsigned char)j->s[j->pos]))j->pos++;}
        else j->bad=true;
        if(j->s[j->pos]=='.'){j->pos++;if(!isdigit((unsigned char)j->s[j->pos]))j->bad=true;while(isdigit((unsigned char)j->s[j->pos]))j->pos++;}
        if(j->s[j->pos]=='e'||j->s[j->pos]=='E'){j->pos++;if(j->s[j->pos]=='+'||j->s[j->pos]=='-')j->pos++;if(!isdigit((unsigned char)j->s[j->pos]))j->bad=true;while(isdigit((unsigned char)j->s[j->pos]))j->pos++;}
        if(j->pos==start)j->bad=true;j->v[t].type='0';
    }
    j->v[t].end=j->pos;j->v[t].next=j->n;return t;
}
static bool jparse(struct json *j,const char *s){*j=(struct json){.s=s?s:""};jvalue(j,0);jspace(j);return !j->bad&&!j->s[j->pos];}
static bool jeq(const struct json *j,int t,const char *s){return t>=0&&t<j->n&&j->v[t].type=='"'&&j->v[t].end-j->v[t].start==strlen(s)+2&&!memcmp(j->s+j->v[t].start+1,s,strlen(s));}
static int jget(const struct json *j,int t,const char *key){
    if(t<0||t>=j->n||j->v[t].type!='{')return -1;
    for(int k=t+1;k<j->v[t].next;){int v=k+1;if(jeq(j,k,key))return v;k=j->v[v].next;}return -1;
}
static int jidx(const struct json *j,int t,int index){if(t<0||j->v[t].type!='[')return -1;int k=t+1;while(index--&&k<j->v[t].next)k=j->v[k].next;return k<j->v[t].next?k:-1;}
static int jbool(const struct json *j,int t){if(t<0)return -1;return j->v[t].type=='t'?1:j->v[t].type=='f'?0:-1;}
static void jraw(FILE *out,const struct json *j,int t){if(t<0)fputs("null",out);else fwrite(j->s+j->v[t].start,1,j->v[t].end-j->v[t].start,out);}
static char *jtext(const struct json *j,int t){
    if(t<0||j->v[t].type!='"')return NULL;size_t a=j->v[t].start+1,b=j->v[t].end-1;char *s=malloc(b-a+1);size_t n=0;
    while(a<b){char c=j->s[a++];if(c=='\\'){c=j->s[a++];if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='f')c='\f';else if(c=='u'){free(s);return NULL;}}s[n++]=c;}s[n]=0;return s;
}
static bool jlong(const struct json *j,int t,int64_t *value){if(t<0||j->v[t].type!='0')return false;char *end;errno=0;long long v=strtoll(j->s+j->v[t].start,&end,10);if(errno||end!=j->s+j->v[t].end)return false;*value=v;return true;}
/* Capture fixed argv only. Bounded output, child group cleanup and timeout. */
static int run_command(char *const args[],int timeout,char **output){
    int pipes[2];if(pipe2(pipes,O_CLOEXEC))return -1;pid_t pid=fork();if(pid<0){close(pipes[0]);close(pipes[1]);return -1;}
    if(!pid){setpgid(0,0);dup2(pipes[1],STDOUT_FILENO);int n=open("/dev/null",O_RDWR);if(n>=0){dup2(n,STDIN_FILENO);dup2(n,STDERR_FILENO);}close(pipes[0]);close(pipes[1]);setenv("LC_ALL","C",1);
#ifdef W202_TESTING
        const char *commands=getenv("W202_COMMAND_ROOT");if(commands){char *exe=path_join(commands,args[0]);execv(exe,args);_exit(127);}
#endif
        execv(args[0],args);_exit(127);}
    setpgid(pid,pid);close(pipes[1]);fcntl(pipes[0],F_SETFL,O_NONBLOCK);size_t used=0,cap=4096;char *buf=malloc(cap);bool done=false,eof=false,failed=false;int st=0;int64_t deadline=mono_ms()+timeout*1000;
    while(!done||!eof){
        if(mono_ms()>=deadline){failed=true;break;}
        struct pollfd p={pipes[0],POLLIN|POLLHUP,0};poll(&p,1,20);
        if(!eof)for(;;){if(used+1024+1>cap){cap*=2;if(cap>1048576){failed=true;break;}buf=realloc(buf,cap);if(!buf)abort();}
            ssize_t n=read(pipes[0],buf+used,cap-used-1);if(n>0){used+=n;continue;}if(!n)eof=true;else if(errno!=EAGAIN&&errno!=EINTR)failed=true;break;}
        if(failed)break;if(!done){pid_t p=waitpid(pid,&st,WNOHANG);if(p==pid)done=true;else if(p<0&&errno!=EINTR){failed=true;break;}}
    }
    if(failed){kill(-pid,SIGKILL);if(!done)while(waitpid(pid,&st,0)<0&&errno==EINTR){}}
    close(pipes[0]);buf[used]=0;if(output)*output=buf;else free(buf);return !failed&&WIFEXITED(st)&&!WEXITSTATUS(st)?0:-1;
}
static char *helper_status(const char *path){char *out=NULL;char *a[]={(char*)path,"status",NULL};if(run_command(a,10,&out)){free(out);return NULL;}struct json j;bool valid=jparse(&j,out)&&j.v[0].type=='{';free(j.v);if(!valid){free(out);return NULL;}return out;}
static const char *runtime_root(void){
#ifdef W202_TESTING
    const char *v=getenv("W202_RUNTIME_ROOT");if(v)return v;
#endif
    return "/run";
}
static bool native_root(void){
#ifdef W202_TESTING
    const char *v=getenv("W202_TEST_ROOT");if(v)return !strcmp(v,"1");
#endif
    return geteuid()==0;
}
static char *sysroot(void){
#ifdef W202_TESTING
    const char *s=getenv("W202_SYSFS_ROOT");if(s)return (char*)s;
#endif
    return "/sys";
}
#endif
