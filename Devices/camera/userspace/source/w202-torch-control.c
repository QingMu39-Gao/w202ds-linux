/* SPDX-License-Identifier: GPL-2.0-only */
#include "native.h"
static bool number(const char *p,long *v){char *s=read_text(p),*e;if(!s)return false;errno=0;*v=strtol(s,&e,10);bool ok=!errno&&e!=s&&!*e;free(s);return ok;}
int main(int argc,char **argv){
    if(argc!=2)return json_error("Expected status or brightness");
    char *led=path_join(sysroot(),"class/leds/white:torch"),*value=path_join(led,"brightness"),*limit=path_join(led,"max_brightness");long n,max;
    if(!number(limit,&max)||max<0)return json_error("Torch unavailable");
    if(strcmp(argv[1],"status")){
        if(!*argv[1])return json_error("Expected a brightness level");
        for(const char *p=argv[1];*p;p++)if(!isdigit((unsigned char)*p))return json_error("Expected a brightness level");
        char *end;errno=0;n=strtol(argv[1],&end,10);if(errno||*end||n<0||n>max)return json_error("Brightness exceeds driver range");
        char buf[40];snprintf(buf,sizeof(buf),"%ld\n",n);if(write_text(value,buf))return json_error("Torch write failed");
    }
    if(!number(value,&n))return json_error("Torch read failed");
    printf("{\"brightness\":%ld,\"maximum\":%ld}\n",n,max);free(led);free(value);free(limit);return 0;
}
