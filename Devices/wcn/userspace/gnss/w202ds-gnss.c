/* SPDX-License-Identifier: GPL-2.0-only */
#include "native.h"
#define MAX_PAYLOAD 2038
struct frame {uint8_t kind,sub;uint16_t len;unsigned char payload[MAX_PAYLOAD];};
struct parser {unsigned char data[8192];size_t used;unsigned bad_header,bad_payload,discarded;};
static uint16_t be16(const unsigned char *p){return (uint16_t)p[0]<<8|p[1];}
static void put16(unsigned char *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static void put32le(unsigned char *p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(8*i);}
static uint16_t checksum(const unsigned char *p,size_t n){uint32_t sum=0;for(size_t i=0;i<n;i+=2)sum+=((unsigned)p[i]<<8)|(i+1<n?p[i+1]:0);while(sum>>16)sum=(sum&65535)+(sum>>16);return ~sum;}
static size_t encode(unsigned char *out,int kind,int sub,const unsigned char *payload,size_t n){if(n>MAX_PAYLOAD)return 0;memset(out,0x7e,4);out[4]=kind;out[5]=sub;put16(out+6,n);put16(out+8,checksum(out,8));if(n)memcpy(out+10,payload,n);put16(out+10+n,n?checksum(payload,n):0);return 12+n;}
static void consume(struct parser *p,size_t n){memmove(p->data,p->data+n,p->used-n);p->used-=n;}
static int packet(struct parser *p,struct frame *f){
    while(p->used>=4){size_t pos=0;for(;pos+4<=p->used;pos++)if(!memcmp(p->data+pos,"~~~~",4))break;
        if(pos+4>p->used){size_t n=p->used-3;p->discarded+=n;consume(p,n);return 0;}if(pos){p->discarded+=pos;consume(p,pos);}if(p->used<10)return 0;
        size_t n=be16(p->data+6);if(n>MAX_PAYLOAD||checksum(p->data,10)){p->bad_header++;consume(p,1);continue;}if(p->used<n+12)return 0;
        if(n&&be16(p->data+10+n)!=checksum(p->data+10,n)){p->bad_payload++;consume(p,1);continue;}
        f->kind=p->data[4];f->sub=p->data[5];f->len=n;memcpy(f->payload,p->data+10,n);consume(p,n+12);return 1;
    }return 0;
}
static volatile sig_atomic_t stopping;
static void stop(int s){stopping=s;}
struct session {int fd,lock,raw;bool owned,awake;char *radio;struct parser parser;char nmea[8193];size_t nmea_used;unsigned nmea_count,bad_nmea,nmea_overflow;};
static int send_frame(struct session *s,int kind,int sub,const unsigned char *payload,size_t n){unsigned char out[MAX_PAYLOAD+12];size_t length=encode(out,kind,sub,payload,n),off=0;int64_t end=mono_ms()+2000;while(off<length){ssize_t k=write(s->fd,out+off,length-off);if(k>0){off+=k;continue;}if(k<0&&errno!=EAGAIN&&errno!=EINTR)return -1;int left=end-mono_ms();if(left<=0)return -1;struct pollfd p={s->fd,POLLOUT,0};if(poll(&p,1,left)<0&&errno!=EINTR)return -1;}return 0;}
static void nmea(struct session *s,const unsigned char *data,size_t len){for(size_t i=0;i<len;i++){
    if(data[i]=='\n'){size_t n=s->nmea_used;while(n&&s->nmea[n-1]=='\r')n--;s->nmea[n]=0;char *star=strrchr(s->nmea,'*');if(n&&s->nmea[0]=='$'&&star){unsigned crc=0;for(char *p=s->nmea+1;p<star;p++)crc^=(unsigned char)*p;char *end;errno=0;long value=strtol(star+1,&end,16);if(strlen(star+1)==2&&isxdigit((unsigned char)star[1])&&isxdigit((unsigned char)star[2])&&!errno&&!*end&&value==(long)crc){fwrite(s->nmea,1,n,stdout);fputs("\r\n",stdout);fflush(stdout);s->nmea_count++;}else s->bad_nmea++;}s->nmea_used=0;
    }else if(s->nmea_used<8192)s->nmea[s->nmea_used++]=data[i];else {s->nmea_used=0;s->nmea_overflow++;}
}}
/* A matching ACK confirms protocol state only, never a GNSS fix. */
static int receive(struct session *s,int timeout,int kind,int sub,int first,bool cleanup){int64_t end=mono_ms()+timeout;bool matched=false;while(mono_ms()<end){if(stopping&&!cleanup)return -1;struct pollfd p={s->fd,POLLIN,0};int left=end-mono_ms();if(poll(&p,1,left)>0){unsigned char data[4096];ssize_t n=read(s->fd,data,sizeof(data));if(n<0&&(errno==EAGAIN||errno==EINTR))continue;if(n<=0)return -1;if(s->raw>=0&&write(s->raw,data,n)!=n)return -1;
        if(s->parser.used+(size_t)n>sizeof(s->parser.data))return -1;memcpy(s->parser.data+s->parser.used,data,n);s->parser.used+=n;struct frame f;
        while(packet(&s->parser,&f)){if((f.kind==5&&f.sub==1)||f.kind==12)nmea(s,f.payload,f.len);if(kind>=0&&f.kind==kind&&f.sub==sub&&(first<0||(f.len&&f.payload[0]==first)))matched=true;}if(matched)return 1;
    }else if(errno!=EINTR&&mono_ms()<end){/* Timeout is handled by the deadline. */}}
    return 0;
}
static int start(struct session *s){unsigned char p[8]={1,0};if(receive(s,250,-1,0,-1,false)<0)return -1;s->awake=true;if(send_frame(s,4,1,p,2)||receive(s,3000,4,1,1,false)!=1)return -1;if(send_frame(s,2,1,NULL,0)||receive(s,250,-1,0,-1,false)<0)return -1;
    const int sub[]={33,12,13,34};const unsigned value[]={0,2,1,0};for(int i=0;i<4;i++){put32le(p,value[i]);if(send_frame(s,1,sub[i],p,4))return -1;}
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);int64_t ms=((int64_t)t.tv_sec-315964800)*1000+t.tv_nsec/1000000;put32le(p,ms/604800000);put32le(p+4,ms%604800000);if(send_frame(s,7,4,p,8)||receive(s,3000,8,5,-1,false)!=1)return -1;fputs("{\"event\":\"navigation_acknowledged\"}\n",stderr);return 0;
}
static int release(struct session *s){int result=0;if(s->owned){if(s->awake&&s->fd>=0){unsigned char p[]={2,0};send_frame(s,7,3,NULL,0);receive(s,1000,8,3,-1,true);send_frame(s,4,1,p,2);receive(s,1000,4,1,2,true);}if(write_text(s->radio,"1\n"))result=-1;char *readback=read_text(s->radio);if(!readback||strcmp(readback,"1"))result=-1;free(readback);s->owned=false;}if(s->raw>=0)close(s->raw);if(s->fd>=0)close(s->fd);if(s->lock>=0)close(s->lock);free(s->radio);return result;}
int main(int argc,char **argv){int duration=30;const char *raw=NULL;for(int i=1;i<argc;i++){if(i+1>=argc)return json_error("Expected duration or raw trace path");const char *key=argv[i++],*v=argv[i];if(!strcmp(key,"--raw"))raw=v;else if(!strcmp(key,"--duration")){char *end;long n=strtol(v,&end,10);if(!*v||*end||n<1||n>3600)return json_error("Duration must be 1..3600");duration=n;}else return json_error("Unknown GNSS option");}
    signal(SIGINT,stop);signal(SIGTERM,stop);struct session s={.fd=-1,.lock=-1,.raw=-1};int result=1;char *lockpath=path_join(runtime_root(),"lock/w202ds-gnss.lock");s.lock=open(lockpath,O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);free(lockpath);if(s.lock<0||flock(s.lock,LOCK_EX|LOCK_NB))goto done;
    char *pattern=path_join(sysroot(),"class/rfkill/*");glob_t g={0};glob(pattern,0,NULL,&g);free(pattern);for(size_t i=0;i<g.gl_pathc;i++){char *namepath=path_join(g.gl_pathv[i],"name"),*name=read_text(namepath);free(namepath);if(name&&!strcmp(name,"w202ds-gnss")){if(s.radio){free(name);globfree(&g);goto done;}s.radio=path_join(g.gl_pathv[i],"soft");}free(name);}globfree(&g);if(!s.radio)goto done;char *blocked=read_text(s.radio);bool ready=blocked&&!strcmp(blocked,"1");free(blocked);if(!ready)goto done;
    const char *device="/dev/data0_gnss";
#ifdef W202_TESTING
    if(getenv("W202_GNSS_DEVICE"))device=getenv("W202_GNSS_DEVICE");
#endif
    s.fd=open(device,O_RDWR|O_NONBLOCK|O_NOCTTY|O_CLOEXEC);if(s.fd<0)goto done;s.owned=true;if(write_text(s.radio,"0\n"))goto done;
    if(raw){s.raw=open(raw,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(s.raw<0)goto done;}
    if(start(&s))goto done;if(receive(&s,duration*1000,-1,0,-1,false)<0)goto done;result=0;
 done:if(release(&s))result=1;fprintf(stderr,"{\"event\":\"summary\",\"nmea\":%u,\"bad_header\":%u,\"bad_payload\":%u,\"bad_nmea\":%u,\"nmea_overflow\":%u,\"ok\":%s}\n",s.nmea_count,s.parser.bad_header,s.parser.bad_payload,s.bad_nmea,s.nmea_overflow,result?"false":"true");return stopping?128+stopping:result;
}
