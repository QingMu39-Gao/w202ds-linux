/* SPDX-License-Identifier: GPL-2.0-only */
#include "native.h"
#include <linux/uinput.h>
struct debounce {bool state;int64_t deadline;};
static bool update(struct debounce *d,bool active,int64_t now,int delay){if(!active){d->state=false;d->deadline=0;}else if(!d->state){if(!d->deadline)d->deadline=now+delay;if(now>=d->deadline){d->state=true;d->deadline=0;}}return d->state;}
static volatile sig_atomic_t stopping;
static void stop(int s){stopping=s;}
static int notify_ready(void){const char *n=getenv("NOTIFY_SOCKET");if(!n)return 0;struct sockaddr_un a={.sun_family=AF_UNIX};size_t len=strlen(n);if(len>=sizeof(a.sun_path))return -1;memcpy(a.sun_path,n,len+1);if(*n=='@')a.sun_path[0]=0;int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return -1;int r=sendto(fd,"READY=1",7,0,(void*)&a,offsetof(struct sockaddr_un,sun_path)+len+1);close(fd);return r<0?-1:0;}
static int emit(int fd,int code,bool state){struct input_event e[2]={0};e[0].type=EV_SW;e[0].code=code;e[0].value=state;e[1].type=EV_SYN;e[1].code=SYN_REPORT;return write(fd,e,sizeof(e))==(ssize_t)sizeof(e)?0:-1;}
int main(int argc,char **argv){const char *source=NULL,*name="Hall switch";int code=-1,delay=0;for(int i=1;i<argc;i++){if(i+1>=argc)return json_error("Expected option value");const char *key=argv[i++],*v=argv[i];char *end;long n=strtol(v,&end,10);if(!strcmp(key,"--source"))source=v;else if(!strcmp(key,"--name"))name=v;else if(!strcmp(key,"--switch-code")&&!*end&&n>=0&&n<=SW_MAX)code=n;else if(!strcmp(key,"--active-delay-ms")&&!*end&&n>=0&&n<=60000)delay=n;else return json_error("Invalid Hall argument");}
    if(geteuid()||!source||code<0||strlen(name)>79)return json_error("Expected root, source and valid switch code");int input=open(source,O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(input<0)return json_error("Hall source unavailable");int fd=open("/dev/uinput",O_WRONLY|O_CLOEXEC|O_NONBLOCK);if(fd<0){close(input);return json_error("uinput unavailable");}
    struct uinput_setup setup={.id={BUS_VIRTUAL,0,0,1}};snprintf(setup.name,sizeof(setup.name),"%s",name);
    if(ioctl(fd,UI_SET_EVBIT,EV_SW)||ioctl(fd,UI_SET_SWBIT,code)||ioctl(fd,UI_DEV_SETUP,&setup)||ioctl(fd,UI_DEV_CREATE)){close(fd);close(input);return json_error("Cannot create Hall input switch");}
    signal(SIGTERM,stop);signal(SIGINT,stop);struct debounce d={0};int last=-1,result=0;bool ready=false;
    while(!stopping){char b[16]={0};if(lseek(input,0,SEEK_SET)<0){result=1;break;}ssize_t n=read(input,b,sizeof(b)-1);if(n<=0||(strcmp(b,"0\n")&&strcmp(b,"1\n")&&strcmp(b,"0")&&strcmp(b,"1"))){result=1;break;}bool state=update(&d,b[0]=='1',mono_ms(),delay);
        if(last!=(int)state){if(emit(fd,code,state)){result=1;break;}last=state;}
        if(!ready){if(notify_ready()){result=1;break;}ready=true;}
        int timeout=d.deadline?(int)(d.deadline-mono_ms()):1000;if(timeout<1)timeout=1;if(timeout>1000)timeout=1000;struct pollfd p={input,POLLPRI|POLLERR,0};if(poll(&p,1,timeout)<0&&errno!=EINTR){result=1;break;}
    }
    ioctl(fd,UI_DEV_DESTROY);close(fd);close(input);return result;
}
