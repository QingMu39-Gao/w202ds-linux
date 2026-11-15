/* SPDX-License-Identifier: MIT */
#include "native.h"
#include <ftw.h>
#include <grp.h>
#include <pwd.h>
#include <sys/sysmacros.h>
struct nv_image {const char *name;size_t payload,capacity;dev_t device;};
static struct nv_image nv[]={
    {"nr_fixnv1_a",0xec0000,52428800,0},{"nr_fixnv2_a",0xec0000,52428800,0},
    {"nr_runtimenv1",0xf40000,57671680,0},{"nr_runtimenv2",0xf40000,57671680,0}
};
static uint32_t little32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t folded(uint64_t value){value=(value>>16)+(value&0xffff);return ~(value+(value>>16))&0xffff;}
static int validate_nv(const char *path,size_t payload,size_t capacity){int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;struct stat st;unsigned char data[8192],header[512];int result=-1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(size_t)st.st_size!=capacity||read(fd,header,sizeof(header))!=(ssize_t)sizeof(header))goto done;
    if(little32(header)!=0x4e56||little32(header+4)!=payload||little32(header+12)!=0x65)goto done;uint64_t sum=0;size_t left=payload;while(left){size_t n=left<sizeof(data)?left:sizeof(data),have=0;while(have<n){ssize_t k=read(fd,data+have,n-have);if(k<0&&errno==EINTR)continue;if(k<=0)goto done;have+=k;}for(size_t i=0;i<n;i+=2)sum+=(unsigned)data[i]|(unsigned)data[i+1]<<8;left-=n;}
    unsigned check=little32(header+8)&0xffff;if(check!=folded(sum)&&check!=folded((uint32_t)sum))goto done;result=0;
 done:close(fd);return result;
}
static bool canonical(const char *path){char *real=realpath(path,NULL);bool same=real&&!strcmp(real,path);free(real);return same;}
static bool private_dir(const char *path){struct stat st;return !lstat(path,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0022);}
/* Return 0 for no mapping, 1 for a verified mapping, -1 for ambiguity/error. */
static int loop_info(const char *image,char **node){char *a[]={"/usr/sbin/losetup","--associated",(char*)image,"--json","--output","NAME,BACK-FILE,OFFSET,SIZELIMIT,RO",NULL},*text=NULL;if(run_command(a,15,&text)){free(text);return -1;}struct json j;int result=-1;if(!jparse(&j,text))goto done;int list=jget(&j,0,"loopdevices");if(list<0||j.v[list].type!='[')goto done;int info=jidx(&j,list,0);if(info<0){result=0;goto done;}if(jidx(&j,list,1)>=0)goto done;int64_t offset,limit;char *back=jtext(&j,jget(&j,info,"back-file")),*path=jtext(&j,jget(&j,info,"name"));char *realback=back?realpath(back,NULL):NULL,*realimage=realpath(image,NULL);bool ro=jbool(&j,jget(&j,info,"ro"))!=0;struct stat st;
    if(path&&realback&&realimage&&!strcmp(realback,realimage)&&jlong(&j,jget(&j,info,"offset"),&offset)&&!offset&&jlong(&j,jget(&j,info,"sizelimit"),&limit)&&!limit&&!ro&&!stat(path,&st)&&S_ISBLK(st.st_mode)&&major(st.st_rdev)==7){*node=path;path=NULL;result=1;}free(path);free(back);free(realback);free(realimage);
 done:free(j.v);free(text);return result;
}
static int verify_node(const char *node,dev_t device){struct stat st;return lstat(node,&st)||!S_ISBLK(st.st_mode)||st.st_rdev!=device?-1:0;}
static const char *expected_by_name;
static int scan_node(const char *path,const struct stat *st,int type,struct FTW *walk){(void)walk;if(type==FTW_NS||type==FTW_DNR)return 1;if(!S_ISBLK(st->st_mode))return 0;char *copy=strdup(path),*base=strrchr(copy,'/');if(!base){free(copy);return 1;}*base++=0;int result=1;if(!strcmp(copy,expected_by_name))for(size_t i=0;i<4;i++)if(!strcmp(base,nv[i].name)&&st->st_rdev==nv[i].device)result=0;free(copy);return result;}
int main(int argc,char **argv){if(argc<4||!native_root())return json_error("Expected root, operation and --state directory");const char *operation=argv[1],*state=NULL;bool check=false;if(strcmp(operation,"prepare")&&strcmp(operation,"verify")&&strcmp(operation,"release"))return json_error("Invalid storage operation");for(int i=2;i<argc;i++){if(!strcmp(argv[i],"--check-nv"))check=true;else if(!strcmp(argv[i],"--state")&&i+1<argc&&!state)state=argv[++i];else return json_error("Invalid storage argument");}
    if(!state||strncmp(state,"/var/lib/w202ds/",16)||!canonical(state)||!private_dir(state))return json_error("Expected canonical private state below /var/lib/w202ds");char *root=path_join(state,"android"),*byname=path_join(root,"dev/block/by-name"),*images=path_join(state,"nv");struct stat st;if(!private_dir(root)||!canonical(byname)||stat(byname,&st)||!S_ISDIR(st.st_mode)||(lstat(images,&st)==0&&S_ISLNK(st.st_mode)))return json_error("Invalid private directory");
    bool prepare=!strcmp(operation,"prepare"),release=!strcmp(operation,"release");if(prepare){if(mkdir(images,0700)&&errno!=EEXIST)return json_error("Cannot create NV directory");if(chown(images,0,0)||chmod(images,0700))return json_error("Cannot protect NV directory");}struct passwd *user=getpwnam("w202modem");struct group *group=getgrnam("w202modem");if(!release&&(!user||!group))return json_error("Missing modem service identity");uid_t uid=user?user->pw_uid:0;gid_t gid=group?group->gr_gid:0;
    int result=1;for(size_t i=0;i<4;i++){char *filename=NULL;asprintf(&filename,"%s.img",nv[i].name);char *image=path_join(images,filename),*node=path_join(byname,nv[i].name),*loop=NULL;free(filename);int r=-1;
        if(release){if(!stat(image,&st)&&S_ISREG(st.st_mode)){int mapped=loop_info(image,&loop);if(mapped<0)goto item_done;if(mapped==1){char *a[]={"/usr/sbin/losetup","--detach",loop,NULL};if(run_command(a,15,NULL))goto item_done;}}r=0;goto item_done;}
        if(prepare&&!exists(image)){if(validate_nv(node,nv[i].payload,nv[i].capacity)||rename(node,image))goto item_done;}
        if((prepare||check)&&validate_nv(image,nv[i].payload,nv[i].capacity))goto item_done;
        if(lstat(image,&st)||!S_ISREG(st.st_mode)||(size_t)st.st_size!=nv[i].capacity||st.st_nlink!=1)goto item_done;
        if(prepare){if(chown(image,0,0)||chmod(image,0600))goto item_done;}else if(st.st_uid||st.st_mode&0077)goto item_done;
        int mapped=loop_info(image,&loop);if(mapped<0)goto item_done;if(!mapped&&prepare){char *a[]={"/usr/sbin/losetup","--find","--show","--nooverlap",image,NULL};if(run_command(a,15,NULL)||loop_info(image,&loop)!=1)goto item_done;}if(!loop)goto item_done;struct stat backing;if(stat(loop,&backing))goto item_done;nv[i].device=backing.st_rdev;
        if(prepare){if(!lstat(node,&st)){if(!S_ISBLK(st.st_mode)||major(st.st_rdev)!=7)goto item_done;if(st.st_rdev!=backing.st_rdev&&unlink(node))goto item_done;}else if(errno!=ENOENT)goto item_done;
            if(lstat(node,&st)){if(errno!=ENOENT||mknod(node,S_IFBLK|0600,backing.st_rdev))goto item_done;}if(chown(node,uid,gid)||chmod(node,0600))goto item_done;}
        r=verify_node(node,backing.st_rdev);
 item_done:free(image);free(node);free(loop);if(r)goto done;}
    if(!release){char *dev=path_join(root,"dev");expected_by_name=byname;int r=nftw(dev,scan_node,16,FTW_PHYS);free(dev);if(r)goto done;puts("Private NV loop mappings verified; physical eMMC is not exposed.");}result=0;
 done:free(root);free(byname);free(images);if(result)fputs("w202-modem-storage: private NV validation or loop mapping failed\n",stderr);return result;
}
