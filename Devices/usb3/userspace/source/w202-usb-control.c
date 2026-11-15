/* SPDX-License-Identifier: GPL-2.0-only */
#include "native.h"
static const char *names[]={"port_type","data_role","power_role"};
static char *role(int attribute){char *pattern=path_join(sysroot(),"class/typec/port*/port_type");glob_t g={0};glob(pattern,0,NULL,&g);free(pattern);char *v=NULL;if(g.gl_pathc==1){char *dir=strdup(g.gl_pathv[0]);*strrchr(dir,'/')=0;char *p=path_join(dir,names[attribute]),*s=read_text(p);if(s)v=selected(s);free(s);free(p);free(dir);}globfree(&g);return v;}
/* Port roles and gadget binding persist after unplug; report attachment separately. */
static int connected(void){
    char *pattern=path_join(sysroot(),"class/typec/port*/port_type");glob_t ports={0};glob(pattern,0,NULL,&ports);free(pattern);int result=-1;
    if(ports.gl_pathc==1){char *dir=strdup(ports.gl_pathv[0]);*strrchr(dir,'/')=0;char *partner=NULL;asprintf(&partner,"%s-partner",dir);struct stat st;if(!stat(partner,&st))result=1;else if(errno==ENOENT)result=0;free(partner);free(dir);}
    globfree(&ports);return result;
}
static int status(void){char *values[3];for(int i=0;i<3;i++)values[i]=role(i);printf("{\"ok\":true,\"available\":%s",values[0]?"true":"false");for(int i=0;i<3;i++){printf(",\"%s\":",names[i]);json_string(stdout,values[i]);free(values[i]);}
    char *pattern=path_join(sysroot(),"kernel/config/usb_gadget/*/UDC");glob_t g={0};glob(pattern,0,NULL,&g);free(pattern);bool known=g.gl_pathc==1;printf(",\"gadget_available\":%s,\"bound\":",known?"true":"false");char *udc=known?read_text(g.gl_pathv[0]):NULL;if(udc)printf("%s",*udc?"true":"false");else fputs("null",stdout);
    for(int i=0;i<2;i++){printf(",\"%s\":",i?"ncm":"acm");if(!known)fputs("null",stdout);else{char *base=strdup(g.gl_pathv[0]);*strrchr(base,'/')=0;char *p=path_join(base,i?"configs/c.1/ncm.usb0":"configs/c.1/acm.GS0");struct stat st;printf("%s",!lstat(p,&st)&&S_ISLNK(st.st_mode)?"true":"false");free(p);free(base);}}
    int attached=connected();printf(",\"connected\":%s",attached<0?"null":attached?"true":"false");
    char *state=NULL;if(udc&&*udc&&!strchr(udc,'/')){char *base=path_join(sysroot(),"class/udc"),*controller=path_join(base,udc),*path=path_join(controller,"state");state=read_text(path);free(path);free(controller);free(base);}
    fputs(",\"link_state\":",stdout);json_string(stdout,state);
    printf(",\"configured\":%s",attached==0?"false":state?(!strcmp(state,"configured")?"true":"false"):"null");
    puts("}");free(state);free(udc);globfree(&g);return 0;}
/* Check ownership before any role request can disconnect the current gadget. */
static bool owned(void){
    char *pattern=path_join(sysroot(),"class/udc/*");glob_t g={0};glob(pattern,0,NULL,&g);free(pattern);bool ok=g.gl_pathc<=1;globfree(&g);if(!ok)return false;
    pattern=path_join(sysroot(),"kernel/config/usb_gadget/*/UDC");memset(&g,0,sizeof(g));glob(pattern,0,NULL,&g);free(pattern);if(g.gl_pathc>1){globfree(&g);return false;}
    if(g.gl_pathc==1){char *base=strdup(g.gl_pathv[0]);*strrchr(base,'/')=0;char *dir=path_join(base,"configs/c.1");DIR *d=opendir(dir);if(!d)ok=false;else{struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char *p=path_join(dir,e->d_name);struct stat st;if(lstat(p,&st))ok=false;else if(S_ISLNK(st.st_mode)){if(strcmp(e->d_name,"acm.GS0")&&strcmp(e->d_name,"ncm.usb0"))ok=false;char *functions=path_join(base,"functions"),*target=path_join(functions,e->d_name),*a=realpath(p,NULL),*b=realpath(target,NULL);if(!a||!b||strcmp(a,b))ok=false;free(functions);free(target);free(a);free(b);}free(p);}closedir(d);}free(dir);free(base);}
    globfree(&g);return ok;
}
static bool valid_mode(const char *s){return !strcmp(s,"auto")||!strcmp(s,"device")||!strcmp(s,"host");}
int main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"status"))return status();if(argc!=5||(strcmp(argv[1],"apply")&&strcmp(argv[1],"rebind"))||!valid_mode(argv[2])||(strcmp(argv[3],"0")&&strcmp(argv[3],"1"))||(strcmp(argv[4],"acm")&&strcmp(argv[4],"ncm")&&strcmp(argv[4],"acm,ncm")))return json_error("Invalid USB request");if(!native_root())return json_error("Root required");
    if(!owned())return json_error("Ambiguous USB controller or foreign gadget functions");
    /* TCPM coordinates CC, VBUS and DWC3; never force the DWC3 role. */
    char *a[]={"/usr/libexec/w202-usb-role",argv[2],NULL};if(!strcmp(argv[1],"apply")&&run_command(a,20,NULL))return json_error("Type-C request rejected");
    setenv("USB_ENABLED",!strcmp(argv[2],"host")?"0":argv[3],1);setenv("USB_FUNCTIONS",argv[4],1);
    char *b[]={"/usr/libexec/w202-usb-gadget","start",NULL};if(run_command(b,40,NULL))return json_error("Gadget request failed; read actual state");return status();}
