/* Native, optional Windows performance counters. Included by main.c.
 * No external processes or privileged sensor drivers. */
#include <pdh.h>
#include <iphlpapi.h>
#include <netioapi.h>
enum { MON_CPU,MON_RAM,MON_DISK,MON_GPU,MON_NET,MON_COUNT };
enum { MON_ATTRS=5 };
typedef struct { int columns,interval,order[MON_COUNT],mask[MON_COUNT]; } MonitorConfig;
static MonitorConfig monitor={2,3000,{0,1,2,3,4},{1,1,1,1,3}};
static double monitor_value[MON_COUNT][MON_ATTRS];
static const int monitor_attrs[MON_COUNT]={5,3,5,5,4};
static const wchar_t *monitor_name(int kind){
    switch(kind){case MON_CPU:return L"CPU";case MON_RAM:return nova_text(L"内存",L"RAM");case MON_DISK:return nova_text(L"硬盘",L"Disk");case MON_GPU:return L"GPU";default:return nova_text(L"网卡",L"Network");}
}
static const wchar_t *monitor_attr(int kind,int a){
    static const wchar_t *zh[MON_COUNT][MON_ATTRS]={{L"使用率",L"频率",L"核心温度",L"封装功耗",L"ACPI 热区"},{L"使用率",L"已用",L"可用"},{L"容量",L"读取",L"写入",L"活动率",L"温度"},{L"使用率",L"温度",L"显存",L"频率",L"功耗"},{L"下载",L"上传",L"链路速率",L"在线网卡"}};
    static const wchar_t *en[MON_COUNT][MON_ATTRS]={{L"Usage",L"Clock",L"Core temp",L"Package power",L"ACPI zone"},{L"Usage",L"Used",L"Free"},{L"Capacity",L"Read",L"Write",L"Active",L"Temp"},{L"Usage",L"Temp",L"VRAM",L"Clock",L"Power"},{L"Down",L"Up",L"Link",L"Active NICs"}};
    return nova_text(zh[kind][a],en[kind][a]);
}
static BOOL monitor_percent(int kind,int a){return (a==0&&kind!=MON_NET)||(kind==MON_DISK&&a==3);}
static void monitor_format(int kind,int a,wchar_t *out,int cap){
    double v=kind==MON_CPU&&a==0?system_cpu:kind==MON_RAM&&a==0?memory_load:monitor_value[kind][a];
    if(v<0){lstrcpynW(out,nova_text(L"暂无数据",L"N/A"),cap);return;}
    const wchar_t *unit=L"";int digits=0;
    if(monitor_percent(kind,a))unit=L"%";
    else if((kind==MON_CPU&&a==1)||(kind==MON_GPU&&a==3))unit=L" MHz";
    else if((kind==MON_CPU&&(a==2||a==4))||(kind==MON_DISK&&a==4)||(kind==MON_GPU&&a==1))unit=L"°C";
    else if((kind==MON_CPU&&a==3)||(kind==MON_GPU&&a==4))unit=L" W";
    else if(kind==MON_RAM||(kind==MON_GPU&&a==2)){unit=L" GB";digits=1;}
    else if(kind==MON_DISK||(kind==MON_NET&&a<2)){unit=L" MB/s";digits=1;}
    else if(kind==MON_NET&&a==2){unit=L" Mb/s";}
    swprintf(out,cap,digits?L"%.1f%ls":L"%.0f%ls",v,unit);
}
static void monitor_load(void){
    int n=store_int(L"app",L"Monitor",L"IntervalMs",3000);monitor.interval=n>=250&&n<=3600000?n:3000;
    n=store_int(L"app",L"Monitor",L"Columns",2);monitor.columns=n>=1&&n<=3?n:2;
    int seen=0;BOOL valid=TRUE;
    for(int i=0;i<MON_COUNT;i++){wchar_t key[20];swprintf(key,20,L"Order%d",i);n=store_int(L"app",L"Monitor",key,i);if(n<0||n>=MON_COUNT||(seen&(1<<n)))valid=FALSE;else seen|=1<<n;monitor.order[i]=n;
        swprintf(key,20,L"Mask%d",i);n=store_int(L"app",L"Monitor",key,i==MON_NET?3:1);monitor.mask[i]=n>=0&&n<(1<<monitor_attrs[i])?n:1;}
    if(!valid)for(int i=0;i<MON_COUNT;i++)monitor.order[i]=i;
}
static BOOL monitor_save(void){
    NovaLogSpan span=nova_log_begin("monitor","settings_save",0);
    if(storage_failed||!store_begin())return nova_log_end(span,FALSE,GetLastError());
    BOOL ok=store_set_int(L"app",L"Monitor",L"IntervalMs",monitor.interval)&&store_set_int(L"app",L"Monitor",L"Columns",monitor.columns);
    for(int i=0;i<MON_COUNT&&ok;i++){wchar_t key[20];swprintf(key,20,L"Order%d",i);ok=store_set_int(L"app",L"Monitor",key,monitor.order[i]);swprintf(key,20,L"Mask%d",i);ok=ok&&store_set_int(L"app",L"Monitor",key,monitor.mask[i]);}
    ok=store_end(ok);return nova_log_end(span,ok,ok?0:GetLastError());
}
static int monitor_count(void){int n=0;for(int i=0;i<MON_COUNT;i++)if(monitor.mask[i])n++;return n;}
typedef PDH_STATUS (WINAPI *MonOpen)(LPCWSTR,DWORD_PTR,PDH_HQUERY*);
typedef PDH_STATUS (WINAPI *MonAdd)(PDH_HQUERY,LPCWSTR,DWORD_PTR,PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI *MonCollect)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *MonGet)(PDH_HCOUNTER,DWORD,LPDWORD,PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS (WINAPI *MonArray)(PDH_HCOUNTER,DWORD,LPDWORD,LPDWORD,PPDH_FMT_COUNTERVALUE_ITEM_W);
typedef PDH_STATUS (WINAPI *MonClose)(PDH_HQUERY);
static HMODULE monitor_pdh;
static PDH_HQUERY monitor_query;
static PDH_HCOUNTER monitor_counters[11];
static HMODULE monitor_iphlp;
static DWORD (WINAPI *monitor_if_table)(PMIB_IF_TABLE2*);
static VOID (WINAPI *monitor_if_free)(PVOID);
static BOOL monitor_if_attempted;
static HMODULE monitor_nvml;
static int (*monitor_nvml_shutdown)(void);
static int (*monitor_nvml_handle)(unsigned,void**);
static int (*monitor_nvml_temp)(void*,unsigned,unsigned*);
static int (*monitor_nvml_clock)(void*,unsigned,unsigned*);
static int (*monitor_nvml_power)(void*,unsigned*);
static BOOL monitor_nvml_ready;
static MonCollect monitor_collect;
static MonGet monitor_get;
static MonArray monitor_array;
static MonClose monitor_close_query;
static BOOL monitor_initialized;
static HMODULE monitor_system_dll(const wchar_t *name){
    wchar_t path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n||n>=MAX_PATH-32)return NULL;
    lstrcatW(path,L"\\");lstrcatW(path,name);return LoadLibraryW(path);
}
static void monitor_init(void){
    if(monitor_initialized)return;
    monitor_initialized=TRUE;monitor_pdh=monitor_system_dll(L"pdh.dll");if(!monitor_pdh)return;
    MonOpen open=(MonOpen)(void*)GetProcAddress(monitor_pdh,"PdhOpenQueryW");
    MonAdd add=(MonAdd)(void*)GetProcAddress(monitor_pdh,"PdhAddEnglishCounterW");
    monitor_collect=(MonCollect)(void*)GetProcAddress(monitor_pdh,"PdhCollectQueryData");
    monitor_get=(MonGet)(void*)GetProcAddress(monitor_pdh,"PdhGetFormattedCounterValue");
    monitor_array=(MonArray)(void*)GetProcAddress(monitor_pdh,"PdhGetFormattedCounterArrayW");
    monitor_close_query=(MonClose)(void*)GetProcAddress(monitor_pdh,"PdhCloseQuery");
    if(!open||!add||!monitor_collect||!monitor_get||!monitor_array||!monitor_close_query||open(NULL,0,&monitor_query)!=0)return;
    const wchar_t *paths[]={L"\\Processor Information(_Total)\\Processor Frequency",L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",L"\\PhysicalDisk(_Total)\\% Disk Time",L"\\GPU Engine(*)\\Utilization Percentage",L"\\Network Interface(*)\\Bytes Received/sec",L"\\Network Interface(*)\\Bytes Sent/sec",L"\\GPU Adapter Memory(*)\\Dedicated Usage",L"\\Network Interface(*)\\Current Bandwidth",L"\\Energy Meter(RAPL_Package0_PKG)\\Power",L"\\Thermal Zone Information(*)\\Temperature"};
    int available=0;
    for(int i=0;i<11;i++)if(add(monitor_query,paths[i],0,&monitor_counters[i])==0)available++;else monitor_counters[i]=NULL;
    nova_log_write(NOVA_LOG_INFO,"monitor","counters_ready",L"available=%d total=11",available);
}
static double monitor_counter(int i,BOOL array,BOOL maximum){
    if(!monitor_counters[i])return -1;
    if(!array){PDH_FMT_COUNTERVALUE v;if(monitor_get(monitor_counters[i],PDH_FMT_DOUBLE,NULL,&v)!=0||v.CStatus>1||v.doubleValue<0)return -1;return v.doubleValue;}
    DWORD bytes=0,count=0;
    if(monitor_array(monitor_counters[i],PDH_FMT_DOUBLE,&bytes,&count,NULL)!=(PDH_STATUS)0x800007D2L||!bytes||bytes>4*1024*1024)return -1;
    PPDH_FMT_COUNTERVALUE_ITEM_W items=HeapAlloc(GetProcessHeap(),0,bytes);if(!items)return -1;
    double result=-1;
    if(monitor_array(monitor_counters[i],PDH_FMT_DOUBLE,&bytes,&count,items)==0)
        for(DWORD j=0;j<count;j++)if(items[j].FmtValue.CStatus<=1&&items[j].FmtValue.doubleValue>=0){double x=items[j].FmtValue.doubleValue;if(result<0)result=x;else if(maximum){if(x>result)result=x;}else result+=x;}
    HeapFree(GetProcessHeap(),0,items);return result;
}
/* NVML is optional and loaded only from the system directory, never the current directory. */
static void monitor_gpu_sensors(void){
    static BOOL attempted; if(!attempted){
        attempted=TRUE;monitor_nvml=monitor_system_dll(L"nvml.dll");
        if(monitor_nvml){
            int (*init)(void)=(int(*)(void))(void*)GetProcAddress(monitor_nvml,"nvmlInit_v2");
            monitor_nvml_shutdown=(int(*)(void))(void*)GetProcAddress(monitor_nvml,"nvmlShutdown");
            monitor_nvml_handle=(int(*)(unsigned,void**))(void*)GetProcAddress(monitor_nvml,"nvmlDeviceGetHandleByIndex_v2");
            monitor_nvml_temp=(int(*)(void*,unsigned,unsigned*))(void*)GetProcAddress(monitor_nvml,"nvmlDeviceGetTemperature");
            monitor_nvml_clock=(int(*)(void*,unsigned,unsigned*))(void*)GetProcAddress(monitor_nvml,"nvmlDeviceGetClockInfo");
            monitor_nvml_power=(int(*)(void*,unsigned*))(void*)GetProcAddress(monitor_nvml,"nvmlDeviceGetPowerUsage");
            if(init&&monitor_nvml_shutdown&&monitor_nvml_handle&&init()==0)monitor_nvml_ready=TRUE;
        }
        nova_log_write(NOVA_LOG_INFO,"monitor","gpu_sensor_ready",L"available=%d",monitor_nvml_ready);
    }
    if(!monitor_nvml_ready)return;
    void *device=NULL;unsigned value;
    if(monitor_nvml_handle(0,&device)!=0||!device)return;
    if(monitor_nvml_temp&&monitor_nvml_temp(device,0,&value)==0)monitor_value[MON_GPU][1]=value;
    if(monitor_nvml_clock&&monitor_nvml_clock(device,0,&value)==0)monitor_value[MON_GPU][3]=value;
    if(monitor_nvml_power&&monitor_nvml_power(device,&value)==0)monitor_value[MON_GPU][4]=value/1000.0;
}
static void monitor_network_links(void){
    if(!monitor_if_attempted){
        monitor_if_attempted=TRUE;monitor_iphlp=monitor_system_dll(L"iphlpapi.dll");
        if(monitor_iphlp){monitor_if_table=(DWORD(WINAPI*)(PMIB_IF_TABLE2*))(void*)GetProcAddress(monitor_iphlp,"GetIfTable2");monitor_if_free=(VOID(WINAPI*)(PVOID))(void*)GetProcAddress(monitor_iphlp,"FreeMibTable");}
        nova_log_write(NOVA_LOG_INFO,"monitor","network_provider_ready",L"available=%d",monitor_if_table&&monitor_if_free);
    }
    if(!monitor_if_table||!monitor_if_free)return;
    PMIB_IF_TABLE2 table=NULL;
    if(monitor_if_table(&table)!=NO_ERROR||!table)return;
    double link=0;unsigned active=0;
    for(ULONG i=0;i<table->NumEntries;i++){
        MIB_IF_ROW2 *row=&table->Table[i];
        if(row->OperStatus==IfOperStatusUp&&row->Type!=IF_TYPE_SOFTWARE_LOOPBACK&&row->InterfaceAndOperStatusFlags.HardwareInterface){active++;link+=(double)row->ReceiveLinkSpeed/1000000.0;}
    }
    monitor_value[MON_NET][2]=link;monitor_value[MON_NET][3]=active;monitor_if_free(table);
}
/* Windows 10 storage temperature query ABI. The project targets Win7 headers;
 * use fixed documented wire fields to avoid requiring a newer SDK. */
#define NOVA_STORAGE_DEVICE_TEMPERATURE_PROPERTY 52
#define NOVA_STORAGE_QUERY_PROPERTY_IOCTL CTL_CODE(IOCTL_STORAGE_BASE,0x0500,METHOD_BUFFERED,FILE_ANY_ACCESS)
typedef struct {DWORD property,query,extra;} NovaStorageQuery;
typedef struct {DWORD version,size;SHORT critical,warning;WORD count;BYTE reserved[2];DWORD reserved2[2];} NovaTempHeader;
typedef struct {WORD index;SHORT temperature,over,under;BYTE flags[4];DWORD reserved;} NovaTempInfo;
_Static_assert(sizeof(NovaStorageQuery)==12,"storage query ABI");
_Static_assert(sizeof(NovaTempHeader)==24,"storage temperature header ABI");
_Static_assert(sizeof(NovaTempInfo)==16,"storage temperature info ABI");
static double monitor_disk_temp_parse(const BYTE *buffer,DWORD returned){
    NovaTempHeader header;
    if(returned<sizeof(header))return -1;
    memcpy(&header,buffer,sizeof(header));
    if(header.size<sizeof(header)||header.size>returned||header.count>(header.size-sizeof(header))/sizeof(NovaTempInfo))return -1;
    double highest=-1;
    for(WORD i=0;i<header.count;i++){
        NovaTempInfo info;memcpy(&info,buffer+sizeof(header)+i*sizeof(info),sizeof(info));
        /* The dashboard treats negative values as unavailable. Ignore unknown sentinel and implausible readings. */
        if(info.temperature>=0&&info.temperature<=200&&info.temperature>highest)highest=info.temperature;
    }
    return highest;
}
static double monitor_disk_temperature(void){
    static BOOL logged;static ULONGLONG last_tick;static double cached=-1;
    ULONGLONG now=GetTickCount64();
    if(last_tick&&now-last_tick<1000)return cached;
    last_tick=now;cached=-1;DWORD drives=GetLogicalDrives(),success=0,failures=0;
    for(int i=0;i<26;i++)if(drives&(1UL<<i)){
        wchar_t root[]={L'A'+i,L':',L'\\',0};if(GetDriveTypeW(root)!=DRIVE_FIXED)continue;
        wchar_t path[]={L'\\',L'\\',L'.',L'\\',L'A'+i,L':',0};
        HANDLE disk=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
        if(disk==INVALID_HANDLE_VALUE){failures++;continue;}
        NovaStorageQuery query={NOVA_STORAGE_DEVICE_TEMPERATURE_PROPERTY,0,0};BYTE buffer[4096];DWORD returned=0;
        if(DeviceIoControl(disk,NOVA_STORAGE_QUERY_PROPERTY_IOCTL,&query,sizeof(query),buffer,sizeof(buffer),&returned,NULL)){
            double reading=monitor_disk_temp_parse(buffer,returned);if(reading>=0){success++;if(reading>cached)cached=reading;}else failures++;
        }else failures++;
        CloseHandle(disk);
    }
    if(!logged){logged=TRUE;nova_log_write(NOVA_LOG_INFO,"monitor","disk_temperature_probe",L"available=%d success=%lu failures=%lu",cached>=0,success,failures);}
    return cached;
}
static double monitor_scaled(double v,double divisor){return v<0?-1:v/divisor;}
static void monitor_sample_extra(const MEMORYSTATUSEX *ms){
    for(int i=0;i<MON_COUNT;i++)for(int j=0;j<MON_ATTRS;j++)monitor_value[i][j]=-1;
    if(ms->ullTotalPhys){monitor_value[MON_RAM][1]=(double)(ms->ullTotalPhys-ms->ullAvailPhys)/1073741824.0;monitor_value[MON_RAM][2]=(double)ms->ullAvailPhys/1073741824.0;}
    if(monitor.mask[MON_DISK]){DWORD drives=GetLogicalDrives();double total=0,free_bytes=0;
        for(int i=0;i<26;i++)if(drives&(1UL<<i)){wchar_t root[]={L'A'+i,L':',L'\\',0};ULARGE_INTEGER capacity,free_space;if(GetDriveTypeW(root)==DRIVE_FIXED&&GetDiskFreeSpaceExW(root,NULL,&capacity,&free_space)){total+=(double)capacity.QuadPart;free_bytes+=(double)free_space.QuadPart;}}
        if(total>0)monitor_value[MON_DISK][0]=100.0*(total-free_bytes)/total;}
    if(test_mode)return;
    monitor_init();
    if(monitor_query&&monitor_collect(monitor_query)==0){
        monitor_value[MON_CPU][1]=monitor_counter(0,FALSE,FALSE);
        if(monitor.mask[MON_CPU])monitor_value[MON_CPU][3]=monitor_scaled(monitor_counter(9,FALSE,FALSE),1000);
        if(monitor.mask[MON_CPU]){double kelvin=monitor_counter(10,TRUE,TRUE);if(kelvin>=273.15&&kelvin<473.15)monitor_value[MON_CPU][4]=kelvin-273.15;}
        monitor_value[MON_DISK][1]=monitor_scaled(monitor_counter(1,FALSE,FALSE),1000000);
        monitor_value[MON_DISK][2]=monitor_scaled(monitor_counter(2,FALSE,FALSE),1000000);
        double busy=monitor_counter(3,FALSE,FALSE);monitor_value[MON_DISK][3]=busy>100?100:busy;
        monitor_value[MON_GPU][0]=monitor_counter(4,TRUE,TRUE);
        monitor_value[MON_NET][0]=monitor_scaled(monitor_counter(5,TRUE,FALSE),1000000);
        monitor_value[MON_NET][1]=monitor_scaled(monitor_counter(6,TRUE,FALSE),1000000);
        monitor_value[MON_GPU][2]=monitor_scaled(monitor_counter(7,TRUE,FALSE),1073741824.0);
        monitor_value[MON_NET][2]=monitor_scaled(monitor_counter(8,TRUE,FALSE),1000000);
    }
    if(monitor.mask[MON_DISK])monitor_value[MON_DISK][4]=monitor_disk_temperature();
    if(monitor.mask[MON_GPU])monitor_gpu_sensors();
    if(monitor.mask[MON_NET])monitor_network_links();
}
static void monitor_shutdown(void){if(monitor_iphlp)FreeLibrary(monitor_iphlp);monitor_iphlp=NULL;if(monitor_nvml_ready&&monitor_nvml_shutdown)monitor_nvml_shutdown();if(monitor_nvml)FreeLibrary(monitor_nvml);monitor_nvml=NULL;monitor_nvml_ready=FALSE;if(monitor_query&&monitor_close_query)monitor_close_query(monitor_query);monitor_query=NULL;if(monitor_pdh)FreeLibrary(monitor_pdh);monitor_pdh=NULL;}
