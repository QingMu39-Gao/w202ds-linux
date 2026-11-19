// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "LinuxImage.h"
#include <Guid/FileInfo.h>
#include <Guid/Gpt.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <libfdt.h>
#include "LinuxDisplay.h"
#include "common/include/pj4edk2_linux_chosen.h"
#include "common/include/pj4edk2_linux_config.h"
#include "common/include/pj4edk2_linux_fdt.h"
#include "common/edk2/Progress.h"

#ifdef W202_LINUX_SERVER
#define PROFILE "server"
#define BOOT_PATH L"\\EFI\\DebianServer\\boot.img"
#define META_PATH L"\\EFI\\DebianServer\\payload.bin"
#define ROOT_PATH L"\\EFI\\DebianServer\\root-partuuid"
#else
#define PROFILE "kde"
#define BOOT_PATH L"\\EFI\\DebianKDE\\boot.img"
#define META_PATH L"\\EFI\\DebianKDE\\payload.bin"
#define ROOT_PATH L"\\EFI\\DebianKDE\\root-partuuid"
#endif
VOID W202EnterKernel(UINT64 Entry, UINT64 Dtb);
EFI_STATUS LinuxStopWatchdog(VOID);
STATIC PJ4EDK2_EFI_PROGRESS mProgress;
STATIC EFI_STATUS mProgressStatus=EFI_NOT_READY;

STATIC VOID ProgressStart(EFI_SYSTEM_TABLE *System) {
  PJB_GUID Wire=PJB_BOOT_DISPLAY_GUID; EFI_GUID Guid;
  PJB_BOOT_DISPLAY *Display=NULL; EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINT32 Cells[20]; UINTN I;
  CopyMem(&Guid,&Wire,sizeof(Guid));
  for (I=0;I<System->NumberOfTableEntries;I++)
    if (CompareGuid(&Guid,&System->ConfigurationTable[I].VendorGuid))
      Display=System->ConfigurationTable[I].VendorTable;
  if (!LinuxDisplayCells(Display,Cells)) return;
  mProgressStatus=gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&Gop);
  if (!EFI_ERROR(mProgressStatus))
    mProgressStatus=Pj4edk2ProgressInit(&mProgress,Gop,Display->x,Display->y,
                                      Display->width,Display->height);
  DEBUG((DEBUG_WARN,"LINUX_EFI progress init=%r\n",mProgressStatus));
}
STATIC VOID Progress(CONST CHAR16 *Line) {
  if (mProgress.Gop) mProgressStatus=Pj4edk2ProgressLine(&mProgress,Line);
}

STATIC EFI_STATUS Open(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path,
                       EFI_FILE_PROTOCOL **File, UINT64 *Bytes) {
  EFI_STATUS Status; EFI_FILE_INFO *Info=NULL; UINTN Size=0;
  *File=NULL;
  Status=Root->Open(Root,File,(CHAR16 *)Path,EFI_FILE_MODE_READ,0);
  if (EFI_ERROR(Status)) return Status;
  Status=(*File)->GetInfo(*File,&gEfiFileInfoGuid,&Size,NULL);
  if (Status!=EFI_BUFFER_TOO_SMALL || Size<SIZE_OF_EFI_FILE_INFO || Size>65536) {
    Status=EFI_VOLUME_CORRUPTED; goto Done;
  }
  Info=AllocatePool(Size); if (!Info) { Status=EFI_OUT_OF_RESOURCES; goto Done; }
  Status=(*File)->GetInfo(*File,&gEfiFileInfoGuid,&Size,Info);
  if (!EFI_ERROR(Status)) {
    if (Info->Attribute&EFI_FILE_DIRECTORY) Status=EFI_INVALID_PARAMETER;
    else *Bytes=Info->FileSize;
  }
Done:
  if (Info) FreePool(Info);
  if (EFI_ERROR(Status)) { (*File)->Close(*File); *File=NULL; }
  return Status;
}
STATIC EFI_STATUS Read(EFI_FILE_PROTOCOL *File,UINT64 Offset,UINT64 Bytes,VOID *Buffer) {
  EFI_STATUS Status; UINTN Chunk,Got;
  Status=File->SetPosition(File,Offset); if (EFI_ERROR(Status)) return Status;
  while (Bytes) {
    Chunk=(UINTN)MIN(Bytes,0x10000ULL); Got=Chunk;
    Status=File->Read(File,&Got,Buffer);
    if (EFI_ERROR(Status)) return Status;
    if (Got!=Chunk) return EFI_END_OF_FILE;
    Buffer=(UINT8 *)Buffer+Got; Bytes-=Got;
  }
  return EFI_SUCCESS;
}
/* Configuration follows the loaded application, independent of profile names. */
STATIC EFI_STATUS LoadConfig(EFI_FILE_PROTOCOL *Root,EFI_LOADED_IMAGE_PROTOCOL *Loaded,
                             struct pj4edk2_linux_config *Config) {
  EFI_DEVICE_PATH_PROTOCOL *Node=Loaded->FilePath;
  CHAR16 Path[256]; CHAR8 Text[4096]; UINTN Used=0,Walked=0,I,Count,Slash=0;
  EFI_FILE_PROTOCOL *File=NULL; UINT64 Bytes; EFI_STATUS Status;
  while (Node && !IsDevicePathEnd(Node)) {
    UINTN Length=DevicePathNodeLength(Node);
    if (Length<sizeof(*Node) || (Walked+=Length)>4096) return EFI_VOLUME_CORRUPTED;
    if (DevicePathType(Node)==MEDIA_DEVICE_PATH && DevicePathSubType(Node)==MEDIA_FILEPATH_DP) {
      CONST CHAR16 *Name=((FILEPATH_DEVICE_PATH *)Node)->PathName;
      if (Length<6 || (Length-4)%2) return EFI_VOLUME_CORRUPTED;
      Count=(Length-4)/2;
      for (I=0;I<Count && Name[I];++I) {
        if (Used>=ARRAY_SIZE(Path)-16) return EFI_BAD_BUFFER_SIZE;
        Path[Used++]=Name[I];
      }
      if (I==Count) return EFI_VOLUME_CORRUPTED;
    }
    Node=NextDevicePathNode(Node);
  }
  if (!Used) return EFI_UNSUPPORTED;
  for (I=0;I<Used;++I) if (Path[I]==L'\\' || Path[I]==L'/') Slash=I+1;
  CopyMem(Path+Slash,L"linuxboot.conf",sizeof(L"linuxboot.conf"));
  Status=Open(Root,Path,&File,&Bytes); if (EFI_ERROR(Status)) return Status;
  if (!Bytes || Bytes>sizeof(Text)) { File->Close(File); return EFI_BAD_BUFFER_SIZE; }
  Status=Read(File,0,Bytes,Text); File->Close(File);
  if (EFI_ERROR(Status)) return Status;
  return pj4edk2_linux_config_parse(Text,(UINTN)Bytes,Config)?EFI_SUCCESS:EFI_VOLUME_CORRUPTED;
}
STATIC CONST CHAR16 *ConfigPath(CONST CHAR8 *Source,CHAR16 Path[256]) {
  UINTN I;
  for (I=0;Source[I];++I) Path[I]=Source[I]=='/'?L'\\':(CHAR16)Source[I];
  Path[I]=0; return Path;
}
STATIC UINT64 Be64(CONST VOID *Value) {
  CONST UINT8 *P=Value; UINT64 N=0; UINTN I;
  for (I=0;I<8;++I) N=(N<<8)|P[I];
  return N;
}
STATIC BOOLEAN Overlap(UINT64 Base,UINT64 Size) {
  if (!Size) return FALSE;
  if (Size>MAX_UINT64-Base) return TRUE;
  return Base<LINUX_DTB_BASE+LINUX_DTB_CAPACITY && LINUX_INITRD_BASE<Base+Size;
}
STATIC EFI_STATUS ValidateDtb(CONST VOID *Dtb,UINTN Bytes) {
  INT32 Node,Length,Count,I,Depth=0,Offset=0,Next; CONST UINT8 *Reg;
  UINT32 Tag; UINT64 Address,Size;
  if (Bytes<sizeof(struct fdt_header) || fdt_check_header(Dtb) || fdt_totalsize(Dtb)>Bytes)
    return EFI_VOLUME_CORRUPTED;
  if (fdt_node_check_compatible(Dtb,0,"sprd,ums9620") ||
      fdt_address_cells(Dtb,0)!=2 || fdt_size_cells(Dtb,0)!=2) return EFI_UNSUPPORTED;
  for (;;) {
    Tag=fdt_next_tag(Dtb,Offset,&Next);
    if (Next<0 || Next<=Offset) return EFI_VOLUME_CORRUPTED;
    if (Tag==FDT_BEGIN_NODE) ++Depth;
    else if (Tag==FDT_END_NODE) { if (--Depth<0) return EFI_VOLUME_CORRUPTED; }
    else if (Tag==FDT_PROP) { if (!Depth) return EFI_VOLUME_CORRUPTED; }
    else if (Tag==FDT_END) { if (Depth) return EFI_VOLUME_CORRUPTED; break; }
    else if (Tag!=FDT_NOP) return EFI_VOLUME_CORRUPTED;
    Offset=Next;
  }
  Node=fdt_path_offset(Dtb,"/memory@80000000"); if (Node<0) return EFI_NOT_FOUND;
  Reg=fdt_getprop(Dtb,Node,"reg",&Length);
  if (!Reg || Length!=16 || Be64(Reg)!=0x80000000 ||
      Be64(Reg+8)<LINUX_DTB_BASE+LINUX_DTB_CAPACITY-0x80000000) return EFI_NO_MAPPING;
  Count=fdt_num_mem_rsv(Dtb); if (Count<0) return EFI_VOLUME_CORRUPTED;
  for (I=0;I<Count;++I)
    if (fdt_get_mem_rsv(Dtb,I,&Address,&Size) || Overlap(Address,Size)) return EFI_NO_MAPPING;
  Node=fdt_path_offset(Dtb,"/reserved-memory");
  if (Node<0 || fdt_address_cells(Dtb,Node)!=2 || fdt_size_cells(Dtb,Node)!=2) return EFI_NO_MAPPING;
  fdt_for_each_subnode(Node,Dtb,Node) {
    Reg=fdt_getprop(Dtb,Node,"reg",&Length);
    if (!Reg) { if (Length!=-FDT_ERR_NOTFOUND) return EFI_VOLUME_CORRUPTED; continue; }
    if (Length%16) return EFI_VOLUME_CORRUPTED;
    for (I=0;I<Length;I+=16) if (Overlap(Be64(Reg+I),Be64(Reg+I+8))) return EFI_NO_MAPPING;
  }
  return Node==-FDT_ERR_NOTFOUND?EFI_SUCCESS:EFI_VOLUME_CORRUPTED;
}
STATIC EFI_STATUS PrepareDisplay(VOID *Dtb,INT32 Chosen,EFI_SYSTEM_TABLE *System) {
  PJB_GUID WireGuid=PJB_BOOT_DISPLAY_GUID; EFI_GUID Guid;
  PJB_BOOT_DISPLAY *Display=NULL; EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info; UINT32 V[20];
  INT32 Logo,Length; CONST UINT8 *Reg; UINTN I;
  /* Never reuse a stale LK record when there is no current page request. */
  INT32 R=fdt_delprop(Dtb,Chosen,PJ4EDK2_DISPLAY_PROPERTY);
  if (R && R!=-FDT_ERR_NOTFOUND) return EFI_VOLUME_CORRUPTED;
  CopyMem(&Guid,&WireGuid,sizeof(Guid));
  for (I=0;I<System->NumberOfTableEntries;++I)
    if (CompareGuid(&Guid,&System->ConfigurationTable[I].VendorGuid))
      Display=System->ConfigurationTable[I].VendorTable;
  if (!Display) return EFI_SUCCESS;
  if (!LinuxDisplayCells(Display,V)) return EFI_UNSUPPORTED;
  if (EFI_ERROR(gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&Gop)) ||
      !Gop->Mode || !Gop->Mode->Info) return EFI_UNSUPPORTED;
  Info=Gop->Mode->Info;
  if (Gop->Mode->FrameBufferBase!=0xb0000000 || Gop->Mode->FrameBufferSize!=9216000 ||
      Info->HorizontalResolution!=1920 || Info->VerticalResolution!=1200 ||
      Info->PixelsPerScanLine!=1920 || Info->PixelFormat!=PixelBlueGreenRedReserved8BitPerColor) return EFI_UNSUPPORTED;
  /* Same native landscape XRGB8888 scanout retained by LkGopDxe. */
  if (MmioRead32(0x31000000)!=0x600 || MmioRead32(0x31000018)!=0x078004b0 ||
      MmioRead32(0x3100002c)!=1 || MmioRead32(0x31000030)!=0xb0000000 ||
      (MmioRead32(0x31000040)&0x007000f0)!=0x00100030 || MmioRead32(0x3100004c)!=1920)
    return EFI_UNSUPPORTED;
  Logo=fdt_path_offset(Dtb,"/reserved-memory/logobuffer@b0000000");
  if (Logo<0 || fdt_getprop(Dtb,Logo,"no-map",NULL) || fdt_getprop(Dtb,Logo,"reusable",NULL))
    return EFI_NO_MAPPING;
  Reg=fdt_getprop(Dtb,Logo,"reg",&Length);
  if (!Reg || Length!=16 || Be64(Reg)!=0xb0000000 || Be64(Reg+8)!=0x9e4000)
    return EFI_NO_MAPPING;
  if (pj4edk2_linux_fdt_display(Dtb,V)) return EFI_VOLUME_CORRUPTED;
  DEBUG((DEBUG_WARN,"LINUX_EFI display=%ux%u rect=%u,%u,%u,%u\n",V[10],V[11],V[12],V[13],V[14],V[15]));
  return EFI_SUCCESS;
}
STATIC EFI_STATUS PrepareDtb(CONST VOID *Source,UINT64 InitrdBytes,EFI_SYSTEM_TABLE *System,
                             CONST UINT64 Times[5],UINT64 PrepareStarted,CONST CHAR8 *RootPartuuid,
                             CONST CHAR8 *Options) {
  VOID *Dtb=(VOID *)(UINTN)LINUX_DTB_BASE; INT32 Chosen,Result; UINTN I;
  fdt64_t LoadTimes[6];
  CONST CHAR8 *Drop[]={"kaslr-seed","rng-seed","linux,uefi-system-table","linux,uefi-mmap-start",
    "linux,uefi-mmap-size","linux,uefi-mmap-desc-size","linux,uefi-mmap-desc-ver"};
  if (fdt_open_into(Source,Dtb,(INT32)LINUX_DTB_CAPACITY)) return EFI_VOLUME_CORRUPTED;
  Chosen=fdt_path_offset(Dtb,"/chosen"); if (Chosen<0) return EFI_NOT_FOUND;
  if (pj4edk2_linux_fdt_initrd(Dtb,LINUX_INITRD_BASE,InitrdBytes) ||
      fdt_setprop_string(Dtb,Chosen,"w202,uefi-profile",PROFILE) ||
      fdt_setprop_string(Dtb,Chosen,PJ4EDK2_ROOT_PROPERTY,RootPartuuid)) return EFI_VOLUME_CORRUPTED;
  /* Keep board bootargs and append the explicitly selected boot options. */
  if (Options && Options[0]) {
    INT32 Length; CONST CHAR8 *Before=fdt_getprop(Dtb,Chosen,"bootargs",&Length);
    CHAR8 Command[4096]; UINTN Existing=0,Extra=AsciiStrLen(Options);
    if (Before) {
      if (Length<1 || Before[Length-1] || AsciiStrLen(Before)!=(UINTN)Length-1)
        return EFI_VOLUME_CORRUPTED;
      Existing=(UINTN)Length-1;
    } else if (Length!=-FDT_ERR_NOTFOUND) return EFI_VOLUME_CORRUPTED;
    if (Existing+Extra+2>sizeof(Command)) return EFI_BAD_BUFFER_SIZE;
    if (Existing) { CopyMem(Command,Before,Existing); Command[Existing++]=' '; }
    CopyMem(Command+Existing,Options,Extra+1);
    if (fdt_setprop_string(Dtb,Chosen,"bootargs",Command)) return EFI_VOLUME_CORRUPTED;
  }
  for (I=0;I<ARRAY_SIZE(Drop);++I) {
    Result=fdt_delprop(Dtb,Chosen,Drop[I]);
    if (Result && Result!=-FDT_ERR_NOTFOUND) return EFI_VOLUME_CORRUPTED;
  }
  {
    EFI_STATUS Status=PrepareDisplay(Dtb,Chosen,System);
    if (EFI_ERROR(Status)) return Status;
  }
  /* Per-phase durations and successful GOP/HII progress writes. */
  for (I=0;I<5;++I) LoadTimes[I]=cpu_to_fdt64(Times[I]);
  LoadTimes[5]=cpu_to_fdt64(GetTimeInNanoSecond(GetPerformanceCounter()-PrepareStarted)/1000);
  Chosen=fdt_path_offset(Dtb,"/chosen");
  if (fdt_setprop(Dtb,Chosen,"w202,uefi-load-us",LoadTimes,sizeof(LoadTimes))) return EFI_VOLUME_CORRUPTED;
  if (fdt_setprop_u32(Dtb,Chosen,"w202,efi-progress-lines",mProgress.Lines) ||
      fdt_setprop_u64(Dtb,Chosen,"w202,efi-progress-status",mProgressStatus)) return EFI_VOLUME_CORRUPTED;
  return fdt_pack(Dtb)?EFI_VOLUME_CORRUPTED:EFI_SUCCESS;
}
STATIC UINT64 PhaseUs(UINT64 *Previous) {
  UINT64 Now=GetPerformanceCounter(),Us=GetTimeInNanoSecond(Now-*Previous)/1000;
  *Previous=Now;
  return Us;
}
STATIC EFI_STATUS Crc(CONST VOID *Data,UINTN Bytes,UINT32 Expected) {
  UINT32 Actual; EFI_STATUS Status=gBS->CalculateCrc32((VOID *)Data,Bytes,&Actual);
  return EFI_ERROR(Status)?Status:(Actual==Expected?EFI_SUCCESS:EFI_CRC_ERROR);
}
STATIC EFI_STATUS ExitToKernel(EFI_HANDLE Image,UINT64 Entry) {
  EFI_MEMORY_DESCRIPTOR *Map; UINTN Capacity=0,Size,Key,DescSize,Attempt;
  UINT32 Version; EFI_STATUS Status;
  Status=gBS->GetMemoryMap(&Capacity,NULL,&Key,&DescSize,&Version);
  if (Status!=EFI_BUFFER_TOO_SMALL || !DescSize || Capacity>0x100000) return EFI_DEVICE_ERROR;
  Capacity+=32*DescSize; Map=AllocatePool(Capacity); if (!Map) return EFI_OUT_OF_RESOURCES;
  for (Attempt=0;Attempt<2;++Attempt) {
    Size=Capacity;
    Status=gBS->GetMemoryMap(&Size,Map,&Key,&DescSize,&Version);
    if (EFI_ERROR(Status)) { if (!Attempt) { FreePool(Map); return Status; } break; }
    Status=gBS->ExitBootServices(Image,Key);
    if (!EFI_ERROR(Status)) {
      DisableInterrupts();
      if (EFI_ERROR(LinuxStopWatchdog())) CpuDeadLoop();
      W202EnterKernel(Entry,LINUX_DTB_BASE);
    }
    if (Status!=EFI_INVALID_PARAMETER) break;
  }
  CpuDeadLoop(); return EFI_DEVICE_ERROR;
}
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_LOADED_IMAGE_PROTOCOL *Loaded; EFI_PARTITION_INFO_PROTOCOL *Part;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs; EFI_FILE_PROTOCOL *Root=NULL,*Boot=NULL,*File=NULL;
  EFI_STATUS Status; UINT64 El,Bytes=0,BootBytes=0,Started=GetPerformanceCounter();
  UINT64 PhaseStarted=Started,Times[5];
  CHAR8 RootPartuuid[37]; CHAR16 Path[256];
  struct pj4edk2_linux_config Config; BOOLEAN Configured=FALSE; UINT64 KernelOffset=4096;
  UINT8 Header[4096],Kernel[64],Meta[32]; UINT32 KBytes,RBytes,DBytes;
  VOID *Dtb=NULL; LINUX_IMAGE Layout; CONST CHAR8 *Phase="esp";
  DEBUG((DEBUG_WARN,"LINUX_EFI begin profile=%a\n",PROFILE));
  ProgressStart(System);
  Progress(L"Linux EFI: opening system partition");
  __asm__ volatile("mrs %0, CurrentEL":"=r"(El));
  if (El!=8) { Status=EFI_UNSUPPORTED; goto Done; }
  Status=gBS->HandleProtocol(Image,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded); if (EFI_ERROR(Status)) goto Done;
  Status=gBS->HandleProtocol(Loaded->DeviceHandle,&gEfiPartitionInfoProtocolGuid,(VOID **)&Part); if (EFI_ERROR(Status)) goto Done;
  if (Part->Type!=PARTITION_TYPE_GPT ||
      !CompareGuid(&Part->Info.Gpt.PartitionTypeGUID,&gEfiPartTypeSystemPartGuid)) { Status=EFI_NO_MAPPING; goto Done; }
  Status=gBS->HandleProtocol(Loaded->DeviceHandle,&gEfiSimpleFileSystemProtocolGuid,(VOID **)&Fs); if (EFI_ERROR(Status)) goto Done;
  Status=Fs->OpenVolume(Fs,&Root); if (EFI_ERROR(Status)) goto Done;
  Progress(L"Linux EFI: reading boot configuration");
  Phase="config"; Status=LoadConfig(Root,Loaded,&Config);
  if (!EFI_ERROR(Status)) {
    Configured=TRUE; KernelOffset=0; CopyMem(RootPartuuid,Config.root_partuuid,sizeof(RootPartuuid));
  } else if (Status!=EFI_NOT_FOUND) goto Done;
  if (!Configured) {
  Phase="root-partuuid"; Status=Open(Root,ROOT_PATH,&File,&Bytes); if (EFI_ERROR(Status)) goto Done;
  if (Bytes!=36) { Status=EFI_VOLUME_CORRUPTED; goto Done; }
  Status=Read(File,0,Bytes,RootPartuuid); File->Close(File); File=NULL; if (EFI_ERROR(Status)) goto Done;
  if (!pj4edk2_linux_partuuid_valid(RootPartuuid,36)) { Status=EFI_VOLUME_CORRUPTED; goto Done; }
  RootPartuuid[36]=0;
  }
  Progress(L"Linux EFI: checking payload metadata");
  Phase="manifest"; Status=Open(Root,Configured?ConfigPath(Config.metadata,Path):META_PATH,&File,&Bytes); if (EFI_ERROR(Status)) goto Done;
  if (Bytes!=sizeof(Meta)) { Status=EFI_VOLUME_CORRUPTED; goto Done; }
  Status=Read(File,0,Bytes,Meta); File->Close(File); File=NULL; if (EFI_ERROR(Status)) goto Done;
  if (CompareMem(Meta,"W202LX01",8)) { Status=EFI_VOLUME_CORRUPTED; goto Done; }
  KBytes=ReadUnaligned32((UINT32 *)(Meta+8)); RBytes=ReadUnaligned32((UINT32 *)(Meta+12)); DBytes=ReadUnaligned32((UINT32 *)(Meta+16));
  Progress(L"Linux EFI: loading device tree");
  Phase="dtb"; Status=Open(Root,Configured?ConfigPath(Config.dtb,Path):L"\\EFI\\W202DS\\LINUX.DTB",&File,&Bytes); if (EFI_ERROR(Status)) goto Done;
  if (Bytes!=DBytes || Bytes<sizeof(struct fdt_header) || Bytes>0x100000) { Status=EFI_BAD_BUFFER_SIZE; goto Done; }
  Dtb=AllocatePool((UINTN)Bytes); if (!Dtb) { Status=EFI_OUT_OF_RESOURCES; goto Done; }
  Status=Read(File,0,Bytes,Dtb); File->Close(File); File=NULL; if (EFI_ERROR(Status)) goto Done;
  Status=Crc(Dtb,DBytes,ReadUnaligned32((UINT32 *)(Meta+28))); if (EFI_ERROR(Status)) goto Done;
  Status=ValidateDtb(Dtb,DBytes); if (EFI_ERROR(Status)) goto Done;
  Phase="headers"; Status=Open(Root,Configured?ConfigPath(Config.kernel,Path):BOOT_PATH,&Boot,&BootBytes); if (EFI_ERROR(Status)) goto Done;
  if (Configured) {
    if (BootBytes!=KBytes) { Status=EFI_CRC_ERROR; goto Done; }
    ZeroMem(Header,sizeof(Header)); CopyMem(Header,"ANDROID!",8);
    WriteUnaligned32((UINT32 *)(Header+8),KBytes); WriteUnaligned32((UINT32 *)(Header+12),RBytes);
    WriteUnaligned32((UINT32 *)(Header+20),1584); WriteUnaligned32((UINT32 *)(Header+40),4);
    BootBytes=4096+(((UINT64)KBytes+4095)&~4095ULL)+RBytes;
  } else { Status=Read(Boot,0,sizeof(Header),Header); if (EFI_ERROR(Status)) goto Done; }
  Status=Read(Boot,KernelOffset,sizeof(Kernel),Kernel); if (EFI_ERROR(Status)) goto Done;
  Status=LinuxParseImage(Header,sizeof(Header),Kernel,sizeof(Kernel),BootBytes,&Layout); if (EFI_ERROR(Status)) goto Done;
  if (Layout.KernelBytes!=KBytes || Layout.RamdiskBytes!=RBytes) { Status=EFI_CRC_ERROR; goto Done; }
  Times[0]=PhaseUs(&PhaseStarted);
  Progress(L"Linux EFI: loading kernel");
  Phase="kernel"; Status=Read(Boot,KernelOffset,KBytes,(VOID *)(UINTN)Layout.Entry); if (EFI_ERROR(Status)) goto Done;
  Times[1]=PhaseUs(&PhaseStarted);
  Progress(L"Linux EFI: verifying kernel");
  Status=Crc((VOID *)(UINTN)Layout.Entry,KBytes,ReadUnaligned32((UINT32 *)(Meta+20))); if (EFI_ERROR(Status)) goto Done;
  ZeroMem((VOID *)(UINTN)(Layout.Entry+KBytes),(UINTN)(Layout.MemoryBytes-KBytes));
  Times[2]=PhaseUs(&PhaseStarted);
  if (Configured) {
    Boot->Close(Boot); Boot=NULL;
    Phase="initrd-open"; Status=Open(Root,ConfigPath(Config.initramfs,Path),&Boot,&Bytes); if (EFI_ERROR(Status)) goto Done;
    if (Bytes!=RBytes) { Status=EFI_CRC_ERROR; goto Done; }
  }
  Progress(L"Linux EFI: loading initramfs");
  Phase="initrd"; Status=Read(Boot,Configured?0:Layout.RamdiskOffset,RBytes,(VOID *)(UINTN)LINUX_INITRD_BASE); if (EFI_ERROR(Status)) goto Done;
  Times[3]=PhaseUs(&PhaseStarted);
  Progress(L"Linux EFI: verifying initramfs");
  Status=Crc((VOID *)(UINTN)LINUX_INITRD_BASE,RBytes,ReadUnaligned32((UINT32 *)(Meta+24))); if (EFI_ERROR(Status)) goto Done;
  if (*(volatile UINT32 *)(UINTN)LINUX_INITRD_BASE!=0x184c2102) { Status=EFI_UNSUPPORTED; goto Done; }
  Times[4]=PhaseUs(&PhaseStarted);
  Progress(L"Linux EFI: preparing kernel handoff");
  Phase="prepare"; Status=PrepareDtb(Dtb,RBytes,System,Times,PhaseStarted,RootPartuuid,Configured?Config.options:NULL); if (EFI_ERROR(Status)) goto Done;
  Boot->Close(Boot); Boot=NULL; Root->Close(Root); Root=NULL; FreePool(Dtb); Dtb=NULL;
  DEBUG((DEBUG_WARN,"LINUX_EFI ready profile=%a kernel=%u initrd=%u entry=%lx elapsed_ms=%Lu\n",PROFILE,KBytes,RBytes,Layout.Entry,GetTimeInNanoSecond(GetPerformanceCounter()-Started)/1000000));
  Progress(L"Linux EFI: starting Linux; waiting for framebuffer console");
  Pj4edk2ProgressClose(&mProgress);
  Phase="entry"; Status=ExitToKernel(Image,Layout.Entry);
Done:
  if (EFI_ERROR(Status)) Progress(L"Linux EFI: boot failed; returning to menu");
  Pj4edk2ProgressClose(&mProgress);
  if (File) File->Close(File);
  if (Boot) Boot->Close(Boot);
  if (Root) Root->Close(Root);
  if (Dtb) FreePool(Dtb);
  DEBUG((DEBUG_WARN,"LINUX_EFI returned profile=%a phase=%a status=%r\n",PROFILE,Phase,Status));
  return Status;
}
