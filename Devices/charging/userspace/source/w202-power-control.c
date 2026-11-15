/* SPDX-License-Identifier: GPL-2.0-only */
#include "native.h"
struct power {char *root,*supplies,*port,*fast,*normal;};
static char *attr(const char *base,const char *key){if(!base)return NULL;char *p=path_join(base,key),*s=read_text(p);free(p);return s;}
static int put(const char *base,const char *key,const char *value){if(!base){errno=ENOENT;return -1;}char *p=path_join(base,key);int r=write_text(p,value);free(p);return r;}
static bool num(const char *base,const char *key,int64_t *v){char *s=attr(base,key),*end;if(!s)return false;errno=0;*v=strtoll(s,&end,10);bool ok=!errno&&end!=s&&!*end;free(s);return ok;}
static char *choice(const char *base,const char *key){char *s=attr(base,key),*v=selected(s);free(s);return v;}
static char *owner(const char *p){char *s=realpath(p,NULL);if(!s)return NULL;char *a=strrchr(s,'/');if(!a){free(s);return NULL;}*a=0;a=strrchr(s,'/');if(a&&(!strcmp(a+1,"typec")||!strcmp(a+1,"usb_power_delivery")))*a=0;return s;}
static bool cap_name(const char *s){char *end;if(!isdigit((unsigned char)*s))return false;strtol(s,&end,10);if(*end++!=':'||!*end)return false;for(;*end;end++)if(!islower((unsigned char)*end)&&*end!='_')return false;return true;}
static int millivolts(const char *s){if(!s||!isdigit((unsigned char)*s))return -1;char *end;errno=0;long n=strtol(s,&end,10);return !errno&&!strcmp(end,"mV")&&n<=INT_MAX?(int)n:-1;}
static int fixed(struct power *p,const char *name){if(!name)return 0;char *base=path_join(p->root,"class/usb_power_delivery"),*a=path_join(base,name),*pattern=path_join(a,"sink-capabilities/*");glob_t g={0};int mask=0;glob(pattern,0,NULL,&g);
    for(size_t i=0;i<g.gl_pathc;i++){char *s=strrchr(g.gl_pathv[i],'/')+1,*colon=strchr(s,':');if(!colon||strcmp(colon,":fixed_supply")||!cap_name(s))continue;char *v=attr(g.gl_pathv[i],"voltage");int n=millivolts(v);free(v);if(n==5000)mask|=1;else if(n==9000)mask|=2;else if(n>0)mask|=4;}
    globfree(&g);free(pattern);free(a);free(base);return mask;
}
static int init(struct power *p){memset(p,0,sizeof(*p));p->root=strdup(sysroot());p->supplies=path_join(p->root,"class/power_supply");char *pattern=path_join(p->root,"class/typec/port*");glob_t ports={0};glob(pattern,0,NULL,&ports);free(pattern);
    for(size_t i=0;i<ports.gl_pathc;i++){char *a=attr(ports.gl_pathv[i],"port_type");if(!a)continue;free(a);if(p->port){globfree(&ports);return -1;}p->port=strdup(ports.gl_pathv[i]);}globfree(&ports);
    char *controller=p->port?owner(p->port):NULL;pattern=path_join(p->root,"class/usb_power_delivery/pd*");glob_t g={0};glob(pattern,0,NULL,&g);free(pattern);bool ambiguous=false;
    for(size_t i=0;i<g.gl_pathc&&controller;i++){char *o=owner(g.gl_pathv[i]);bool matches=o&&!strcmp(o,controller);free(o);if(!matches)continue;char *c=path_join(g.gl_pathv[i],"sink-capabilities/*");glob_t caps={0};glob(c,0,NULL,&caps);free(c);int min=INT_MAX,max=0;bool valid=true;
        for(size_t k=0;k<caps.gl_pathc;k++){const char *name=strrchr(caps.gl_pathv[k],'/')+1;if(!cap_name(name))continue;struct stat st;if(stat(caps.gl_pathv[k],&st)||!S_ISDIR(st.st_mode))continue;char *v=attr(caps.gl_pathv[k],"voltage");if(!v)v=attr(caps.gl_pathv[k],"maximum_voltage");int n=millivolts(v);free(v);if(n<0){valid=false;break;}if(n<min)min=n;if(n>max)max=n;}
        globfree(&caps);if(valid&&max&&min>=5000){char **slot=max==5000?&p->normal:&p->fast;if(*slot){ambiguous=true;break;}*slot=strdup(strrchr(g.gl_pathv[i],'/')+1);}
    }globfree(&g);free(controller);if(ambiguous){free(p->fast);free(p->normal);p->fast=p->normal=NULL;}return 0;
}
static void finish(struct power *p){free(p->root);free(p->supplies);free(p->port);free(p->fast);free(p->normal);}
static void field_string(FILE *f,const char *key,const char *value){json_string(f,key);fputc(':',f);json_string(f,value);fputc(',',f);}
static void field_num(FILE *f,const char *key,const char *base,const char *attrname){int64_t v;json_string(f,key);fputc(':',f);if(num(base,attrname,&v))fprintf(f,"%lld",(long long)v);else fputs("null",f);fputc(',',f);}
static const char *mode(struct power *p,const char *selected_profile,int64_t gate){if(p->normal&&!strcmp(selected_profile,p->normal)&&gate==0)return "pd5v";if(p->fast&&!strcmp(selected_profile,p->fast)&&(gate==0||gate==1))return gate?"pps":fixed(p,p->fast)==3?"pd9v":"pd-fixed";return NULL;}
static char *status(struct power *p){char *out=NULL;size_t n=0;FILE *f=open_memstream(&out,&n);char *battery=path_join(p->supplies,"w202-battery"),*usb=path_join(p->supplies,"w202-usb-c"),*charger=path_join(p->supplies,"w202-bq-main");int64_t gate=-1,online=0,pd=-1,voltage=-1,vbus=-1;num(charger,"device/fast_charge",&gate);num(usb,"online",&online);num(usb,"voltage_now",&vbus);
    char *profile=p->fast&&p->normal?choice(p->port,"select_usb_power_delivery"):strdup("");const char *m=mode(p,profile,gate);int fast=m?strcmp(m,"pd5v")!=0:-1;
    char *pattern=path_join(p->supplies,"tcpm-source-psy-*");glob_t sources={0};glob(pattern,0,NULL,&sources);free(pattern);char *source=sources.gl_pathc==1?sources.gl_pathv[0]:NULL;if(source){num(source,"online",&pd);num(source,"voltage_now",&voltage);}
    char *protocol=choice(usb,"usb_type");const char *proto=!online?"Disconnected":pd==2?"PD PPS":((!strcmp(protocol,"PD")||!strcmp(protocol,"PD_PPS"))&&pd==1)?"PD fixed":*protocol?protocol:"Unknown";
    const char *active=!strcmp(proto,"PD PPS")?"pps":!strcmp(proto,"PD fixed")&&voltage==5000000?"pd5v":!strcmp(proto,"PD fixed")&&voltage==9000000?"pd9v":NULL;
    char *behaviour=choice(charger,"charge_behaviour"),*port_type=attr(p->port,"port_type"),*role=choice(p->port,"power_role"),*data=choice(p->port,"data_role"),*type=selected(port_type);int separation=!strcmp(behaviour,"inhibit-charge")?1:!strcmp(behaviour,"auto")?0:-1;
    char *partner=p->port?NULL:strdup("");if(p->port&&asprintf(&partner,"%s-partner",p->port)<0)abort();bool connected=exists(partner);
    char *intentpath=path_join(runtime_root(),"w202-power-mode.json"),*intent=read_text(intentpath),*requested=NULL;free(intentpath);if(intent){struct json j;if(jparse(&j,intent))requested=jtext(&j,jget(&j,0,"requested_mode"));free(j.v);free(intent);}if(requested&&strcmp(requested,"auto")&&strcmp(requested,"pps")&&strcmp(requested,"pd5v")&&strcmp(requested,"pd9v")){free(requested);requested=NULL;}
    fputs("{",f);fprintf(f,"\"typec_connected\":%s,\"input_online\":%s,\"usb_output_active\":%s,",connected?"true":"false",online?"true":"false",connected&&!strcmp(role,"source")&&vbus>=4000000?"true":"false");
    field_string(f,"requested_mode",requested?requested:m&&!strcmp(m,"pps")?"auto":m);
    field_num(f,"battery_percent",battery,"capacity");char *bs=attr(battery,"status");field_string(f,"battery_status",bs);free(bs);
    field_num(f,"battery_current_ua",battery,"current_now");field_num(f,"battery_voltage_uv",battery,"voltage_now");field_num(f,"battery_power_uw",battery,"power_now");field_num(f,"battery_temperature_decic",battery,"temp");field_num(f,"input_voltage_uv",usb,"voltage_now");
    if(online&&!strcmp(role,"sink"))field_num(f,"input_current_ua",usb,"current_now");else fputs("\"input_current_ua\":null,",f);
    field_num(f,"contract_current_ua",source,"current_now");field_num(f,"contract_voltage_uv",source,"voltage_now");field_num(f,"input_limit_ua",charger,"input_current_limit");field_string(f,"protocol",proto);
    fprintf(f,"\"separation\":%s,",separation<0?"null":separation?"true":"false");field_num(f,"charge_enabled",charger,"device/charge_enabled");fprintf(f,"\"fast_charge\":%s,",fast<0?"null":fast?"true":"false");field_string(f,"charging_mode",m);field_string(f,"active_mode",active);fprintf(f,"\"pps_allowed\":%s,",gate==0?"false":gate==1?"true":"null");
    fprintf(f,"\"charging_modes\":%s,",p->fast&&p->normal&&(fixed(p,p->fast)&2)?"[\"pd5v\",\"pd9v\",\"pps\"]":"[]");field_string(f,"pd_profile",*profile?profile:NULL);field_string(f,"port_type",type);field_string(f,"data_role",data);field_string(f,"power_role",role);
    fprintf(f,"\"available\":{\"charging\":%s,\"fast_charge\":%s,\"usb_role\":%s}}",separation>=0?"true":"false",p->fast&&p->normal&&(gate==0||gate==1)?"true":"false",port_type&&strstr(port_type,"dual")&&strstr(port_type,"source")?"true":"false");fclose(f);
    globfree(&sources);free(battery);free(usb);free(charger);free(profile);free(protocol);free(behaviour);free(port_type);free(role);free(data);free(type);free(partner);free(requested);return out;
}
static int select_profile(struct power *p,bool fast,bool pps){if(!p->fast||!p->normal)return -1;char *charger=path_join(p->supplies,"w202-bq-main");int r=put(charger,"device/fast_charge","0\n");if(r)goto done;const char *wanted=fast?p->fast:p->normal;char *value=NULL;asprintf(&value,"%s\n",wanted);
    for(int i=0;i<5;i++){r=put(p->port,"select_usb_power_delivery",value);if(!r||errno!=EAGAIN)break;usleep(250000);}free(value);if(r)goto done;char *got=choice(p->port,"select_usb_power_delivery");r=strcmp(got,wanted)?-1:0;free(got);if(!r&&pps)r=put(charger,"device/fast_charge","1\n");
 done:free(charger);return r;
}
static bool allowed(const char *s){const char *a[]={"status","charge-auto","charge-inhibit","fast-on","fast-off","mode-auto","mode-pd5v","mode-pd9v","mode-pps","usb-auto","usb-host","usb-device"};for(size_t i=0;i<sizeof(a)/sizeof(*a);i++)if(!strcmp(a[i],s))return true;return false;}
int main(int argc,char **argv){if(argc!=2||!allowed(argv[1]))return json_error("Unknown power action");struct power p;if(init(&p))return json_error("Ambiguous Type-C port selection");int lock=-1,r=0;
    if(strcmp(argv[1],"status")){
        if(!native_root())return json_error("Administrator authorization required");char *lockpath=path_join(runtime_root(),"w202-power-control.lock");lock=open(lockpath,O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);free(lockpath);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return json_error("Power control busy");const char *s=argv[1];
        if(!strncmp(s,"charge-",7)){char *a[]={"/usr/libexec/w202-charging",!strcmp(s,"charge-auto")?"auto":"inhibit-charge",NULL};r=run_command(a,25,NULL);}
        else if(!strncmp(s,"usb-",4)){char *a[]={"/usr/libexec/w202-usb-role",(char*)s+4,NULL};r=run_command(a,25,NULL);}
        else if(!strcmp(s,"fast-on"))r=select_profile(&p,true,true);else if(!strcmp(s,"fast-off"))r=select_profile(&p,false,false);
        else {const char *m=s+5;if(!strcmp(m,"pd9v")&&fixed(&p,p.fast)!=3)r=-1;else r=select_profile(&p,strcmp(m,"pd5v")!=0,!strcmp(m,"pps")||!strcmp(m,"auto"));
            if(!r){char *intentpath=path_join(runtime_root(),"w202-power-mode.json");int fd=open(intentpath,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);free(intentpath);if(fd<0)r=-1;else {char *v=NULL;asprintf(&v,"{\"requested_mode\":\"%s\"}\n",m);if(write(fd,v,strlen(v))!=(ssize_t)strlen(v))r=-1;/* Non-secret device intent must also be readable by desktop users. */if(!r&&fchmod(fd,0644))r=-1;free(v);close(fd);}}}
    }
    if(r){if(lock>=0)close(lock);finish(&p);return json_error("Power operation failed; PPS remains gated if profile selection failed");}char *out=status(&p);puts(out);free(out);if(lock>=0)close(lock);finish(&p);return 0;
}
