HXCOMM Use DEFHEADING() to define headings.
HXCOMM DEF(option, HAS_ARG/0, opt_enum, opt_help, arch_mask) is used to
HXCOMM construct option structures, enums and help message for specified
HXCOMM architectures.
HXCOMM HXCOMM can be used for comments.

DEFHEADING(Standard options:)

DEF("help", 0, QEMU_OPTION_h,
    "-h or -help     display this help and exit\n", QEMU_ARCH_ALL)

DEF("version", 0, QEMU_OPTION_version,
    "-version        display version information and exit\n", QEMU_ARCH_ALL)

DEF("machine", HAS_ARG, QEMU_OPTION_machine, \
    "-machine [type=]name[,prop[=value][,...]]\n"
    "                selects emulated machine ('-machine help' for list)\n"
    "                property accel=accel1[:accel2[:...]] selects accelerator\n"
    "                supported accelerators are kvm, hvf, whpx or tcg (default: tcg)\n"
    "                vmport=on|off|auto controls emulation of vmport (default: auto)\n"
    "                dump-guest-core=on|off include guest memory in a core dump (default=on)\n"
    "                mem-merge=on|off controls memory merge support (default: on)\n"
    "                aes-key-wrap=on|off controls support for AES key wrapping (default=on)\n"
    "                dea-key-wrap=on|off controls support for DEA key wrapping (default=on)\n"
    "                memory-encryption=@var{} memory encryption object to use (default=none)\n"
#ifdef CONFIG_POSIX
    "                aux-ram-share=on|off allocate auxiliary guest RAM as shared (default: off)\n"
#endif
    "                memory-backend='backend-id' specifies explicitly provided backend for main RAM (default=none)\n"
    "                smp-cache.0.cache=cachename,smp-cache.0.topology=topologylevel\n",
    QEMU_ARCH_ALL)

DEF("M", HAS_ARG, QEMU_OPTION_M,
    "                machine,...\n",
    QEMU_ARCH_ALL)

DEF("cpu", HAS_ARG, QEMU_OPTION_cpu,
    "-cpu cpu        select CPU ('-cpu help' for list)\n", QEMU_ARCH_ALL)

DEF("accel", HAS_ARG, QEMU_OPTION_accel,
    "-accel [accel=]accelerator[,prop[=value][,...]]\n"
    "                select accelerator (kvm, hvf, whpx or tcg; use 'help' for a list)\n"
    "                kvm-shadow-mem=size of KVM shadow MMU in bytes\n"
    "                one-insn-per-tb=on|off (one guest instruction per TCG translation block)\n"
    "                split-wx=on|off (enable TCG split w^x mapping)\n"
    "                tb-size=n (TCG translation block cache size)\n"
    "                dirty-ring-size=n (KVM dirty ring GFN count, default 0)\n"
    "                eager-split-size=n (KVM Eager Page Split chunk size, default 0, disabled. ARM only)\n"
    "                thread=single|multi (enable multi-threaded TCG)\n"
    "                device=path (KVM device path, default /dev/kvm)\n", QEMU_ARCH_ALL)

DEF("smp", HAS_ARG, QEMU_OPTION_smp,
    "-smp [[cpus=]n][,maxcpus=maxcpus][,drawers=drawers][,books=books][,sockets=sockets]\n"
    "               [,dies=dies][,clusters=clusters][,modules=modules][,cores=cores]\n"
    "               [,threads=threads]\n"
    "                set the number of initial CPUs to 'n' [default=1]\n"
    "                maxcpus= maximum number of total CPUs, including\n"
    "                offline CPUs for hotplug, etc\n"
    "                drawers= number of drawers on the machine board\n"
    "                books= number of books in one drawer\n"
    "                sockets= number of sockets in one book\n"
    "                dies= number of dies in one socket\n"
    "                clusters= number of clusters in one die\n"
    "                modules= number of modules in one cluster\n"
    "                cores= number of cores in one module\n"
    "                threads= number of threads in one core\n"
    "Note: Different machines may have different subsets of the CPU topology\n"
    "      parameters supported, so the actual meaning of the supported parameters\n"
    "      will vary accordingly. For example, for a machine type that supports a\n"
    "      three-level CPU hierarchy of sockets/cores/threads, the parameters will\n"
    "      sequentially mean as below:\n"
    "                sockets means the number of sockets on the machine board\n"
    "                cores means the number of cores in one socket\n"
    "                threads means the number of threads in one core\n"
    "      For a particular machine type board, an expected CPU topology hierarchy\n"
    "      can be defined through the supported sub-option. Unsupported parameters\n"
    "      can also be provided in addition to the sub-option, but their values\n"
    "      must be set as 1 in the purpose of correct parsing.\n",
    QEMU_ARCH_ALL)

DEF("add-fd", HAS_ARG, QEMU_OPTION_add_fd,
    "-add-fd fd=fd,set=set[,opaque=opaque]\n"
    "                Add 'fd' to fd 'set'\n", QEMU_ARCH_ALL)

DEF("set", HAS_ARG, QEMU_OPTION_set,
    "-set group.id.arg=value\n"
    "                set <arg> parameter for item <id> of type <group>\n"
    "                i.e. -set drive.$id.file=/path/to/image\n", QEMU_ARCH_ALL)

DEF("global", HAS_ARG, QEMU_OPTION_global,
    "-global driver.property=value\n"
    "-global driver=driver,property=property,value=value\n"
    "                set a global default for a driver property\n",
    QEMU_ARCH_ALL)

DEF("m", HAS_ARG, QEMU_OPTION_m,
    "-m [size=]megs[,slots=n,maxmem=size]\n"
    "                configure guest RAM\n"
    "                size: initial amount of guest memory\n"
    "                slots: number of hotplug slots (default: none)\n"
    "                maxmem: maximum amount of guest memory (default: none)\n"
    "                Note: Some architectures might enforce a specific granularity\n",
    QEMU_ARCH_ALL)

DEF("mem-path", HAS_ARG, QEMU_OPTION_mempath,
    "-mem-path FILE  provide backing storage for guest RAM\n", QEMU_ARCH_ALL)

DEF("mem-prealloc", 0, QEMU_OPTION_mem_prealloc,
    "-mem-prealloc   preallocate guest memory (use with -mem-path)\n",
    QEMU_ARCH_ALL)

DEF("k", HAS_ARG, QEMU_OPTION_k,
    "-k language     use keyboard layout (for example 'fr' for French)\n",
    QEMU_ARCH_ALL)

DEF("audiodev", HAS_ARG, QEMU_OPTION_audiodev,
    "-audiodev [driver=]driver,id=id[,prop[=value][,...]]\n"
    "                specifies the audio backend to use\n"
    "                Use ``-audiodev help`` to list the available drivers\n"
    "                id= identifier of the backend\n"
    "                timer-period= timer period in microseconds\n"
    "                in|out.mixing-engine= use mixing engine to mix streams inside QEMU\n"
    "                in|out.fixed-settings= use fixed settings for host audio\n"
    "                in|out.frequency= frequency to use with fixed settings\n"
    "                in|out.channels= number of channels to use with fixed settings\n"
    "                in|out.format= sample format to use with fixed settings\n"
    "                valid values: s8, s16, s32, u8, u16, u32, f32\n"
    "                in|out.voices= number of voices to use\n"
    "                in|out.buffer-length= length of buffer in microseconds\n"
    "-audiodev none,id=id,[,prop[=value][,...]]\n"
    "                dummy driver that discards all output\n"
#ifdef CONFIG_AUDIO_ALSA
    "-audiodev alsa,id=id[,prop[=value][,...]]\n"
    "                in|out.dev= name of the audio device to use\n"
    "                in|out.period-length= length of period in microseconds\n"
    "                in|out.try-poll= attempt to use poll mode\n"
    "                threshold= threshold (in microseconds) when playback starts\n"
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
    "-audiodev coreaudio,id=id[,prop[=value][,...]]\n"
    "                in|out.buffer-count= number of buffers\n"
#endif
#ifdef CONFIG_AUDIO_DSOUND
    "-audiodev dsound,id=id[,prop[=value][,...]]\n"
    "                latency= add extra latency to playback in microseconds\n"
#endif
#ifdef CONFIG_AUDIO_OSS
    "-audiodev oss,id=id[,prop[=value][,...]]\n"
    "                in|out.dev= path of the audio device to use\n"
    "                in|out.buffer-count= number of buffers\n"
    "                in|out.try-poll= attempt to use poll mode\n"
    "                try-mmap= try using memory mapped access\n"
    "                exclusive= open device in exclusive mode\n"
    "                dsp-policy= set timing policy (0..10), -1 to use fragment mode\n"
#endif
#ifdef CONFIG_AUDIO_PA
    "-audiodev pa,id=id[,prop[=value][,...]]\n"
    "                server= PulseAudio server address\n"
    "                in|out.name= source/sink device name\n"
    "                in|out.latency= desired latency in microseconds\n"
#endif
#ifdef CONFIG_AUDIO_PIPEWIRE
    "-audiodev pipewire,id=id[,prop[=value][,...]]\n"
    "                in|out.name= source/sink device name\n"
    "                in|out.stream-name= name of pipewire stream\n"
    "                in|out.latency= desired latency in microseconds\n"
#endif
#ifdef CONFIG_AUDIO_SDL
    "-audiodev sdl,id=id[,prop[=value][,...]]\n"
    "                in|out.buffer-count= number of buffers\n"
#endif
#ifdef CONFIG_AUDIO_SNDIO
    "-audiodev sndio,id=id[,prop[=value][,...]]\n"
#endif
    "-audiodev wav,id=id[,prop[=value][,...]]\n"
    "                path= path of wav file to record\n",
    QEMU_ARCH_ALL)

DEF("device", HAS_ARG, QEMU_OPTION_device,
    "-device driver[,prop[=value][,...]]\n"
    "                add device (based on driver)\n"
    "                prop=value,... sets driver properties\n"
    "                use '-device help' to print all possible drivers\n"
    "                use '-device driver,help' to print all possible properties\n",
    QEMU_ARCH_ALL)

DEF("name", HAS_ARG, QEMU_OPTION_name,
    "-name string1[,process=string2][,debug-threads=on|off]\n"
    "                set the name of the guest\n"
    "                string1 sets the window title and string2 the process name\n"
    "                When debug-threads is enabled, individual threads are given a separate name\n"
    "                NOTE: The thread names are for debugging and not a stable API.\n",
    QEMU_ARCH_ALL)

DEF("uuid", HAS_ARG, QEMU_OPTION_uuid,
    "-uuid %08x-%04x-%04x-%04x-%012x\n"
    "                specify machine UUID\n", QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(Block device options:)

DEF("hda", HAS_ARG, QEMU_OPTION_hda,
    "-hda/-hdb file  use 'file' as hard disk 0/1 image\n", QEMU_ARCH_ALL)
DEF("hdb", HAS_ARG, QEMU_OPTION_hdb, "", QEMU_ARCH_ALL)
DEF("hdc", HAS_ARG, QEMU_OPTION_hdc,
    "-hdc/-hdd file  use 'file' as hard disk 2/3 image\n", QEMU_ARCH_ALL)
DEF("hdd", HAS_ARG, QEMU_OPTION_hdd, "", QEMU_ARCH_ALL)

DEF("cdrom", HAS_ARG, QEMU_OPTION_cdrom,
    "-cdrom file     use 'file' as CD-ROM image\n",
    QEMU_ARCH_ALL)

DEF("blockdev", HAS_ARG, QEMU_OPTION_blockdev,
    "-blockdev [driver=]driver[,node-name=N][,discard=ignore|unmap]\n"
    "          [,cache.direct=on|off][,cache.no-flush=on|off]\n"
    "          [,read-only=on|off][,auto-read-only=on|off]\n"
    "          [,force-share=on|off][,detect-zeroes=on|off|unmap]\n"
    "          [,driver specific parameters...]\n"
    "                configure a block backend\n", QEMU_ARCH_ALL)

DEF("drive", HAS_ARG, QEMU_OPTION_drive,
    "-drive [file=file][,if=type][,bus=n][,unit=m][,media=d][,index=i]\n"
    "       [,cache=writethrough|writeback|none|directsync|unsafe][,format=f]\n"
    "       [,snapshot=on|off][,rerror=ignore|stop|report]\n"
    "       [,werror=ignore|stop|report|enospc][,id=name]\n"
    "       [,aio=threads|native|io_uring]\n"
    "       [,readonly=on|off][,copy-on-read=on|off]\n"
    "       [,discard=ignore|unmap][,detect-zeroes=on|off|unmap]\n"
    "       [[,bps=b]|[[,bps_rd=r][,bps_wr=w]]]\n"
    "       [[,iops=i]|[[,iops_rd=r][,iops_wr=w]]]\n"
    "       [[,bps_max=bm]|[[,bps_rd_max=rm][,bps_wr_max=wm]]]\n"
    "       [[,iops_max=im]|[[,iops_rd_max=irm][,iops_wr_max=iwm]]]\n"
    "       [[,iops_size=is]]\n"
    "       [[,group=g]]\n"
    "                use 'file' as a drive image\n", QEMU_ARCH_ALL)

DEF("mtdblock", HAS_ARG, QEMU_OPTION_mtdblock,
    "-mtdblock file  use 'file' as on-board Flash memory image\n",
    QEMU_ARCH_ALL)

DEF("sd", HAS_ARG, QEMU_OPTION_sd,
    "-sd file        use 'file' as SecureDigital card image\n", QEMU_ARCH_ALL)

DEF("snapshot", 0, QEMU_OPTION_snapshot,
    "-snapshot       write to temporary files instead of disk image files\n",
    QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(USB convenience options:)

DEF("usb", 0, QEMU_OPTION_usb,
    "-usb            enable on-board USB host controller (if not enabled by default)\n",
    QEMU_ARCH_ALL)

DEF("usbdevice", HAS_ARG, QEMU_OPTION_usbdevice,
    "-usbdevice name add the host or guest USB device 'name'\n",
    QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(Display options:)

DEF("display", HAS_ARG, QEMU_OPTION_display,
#if defined(CONFIG_SDL)
    "-display sdl[,gl=on|core|es|off][,grab-mod=<mod>][,show-cursor=on|off]\n"
    "            [,window-close=on|off]\n"
#endif
#if defined(CONFIG_GTK)
    "-display gtk[,full-screen=on|off][,gl=on|off][,grab-on-hover=on|off]\n"
    "            [,show-tabs=on|off][,show-cursor=on|off][,window-close=on|off]\n"
    "            [,show-menubar=on|off][,zoom-to-fit=on|off]\n"
#endif
#if defined(CONFIG_VNC)
    "-display vnc=<display>[,<optargs>]\n"
#endif
#if defined(CONFIG_CURSES)
    "-display curses[,charset=<encoding>]\n"
#endif
#if defined(CONFIG_COCOA)
    "-display cocoa[,full-grab=on|off][,swap-opt-cmd=on|off]\n"
    "              [,show-cursor=on|off][,left-command-key=on|off]\n"
    "              [,full-screen=on|off][,zoom-to-fit=on|off]\n"
#endif
    "-display none\n"
    "                select display backend type\n"
    "                The default display is equivalent to\n                "
#if defined(CONFIG_GTK)
            "\"-display gtk\"\n"
#elif defined(CONFIG_SDL)
            "\"-display sdl\"\n"
#elif defined(CONFIG_COCOA)
            "\"-display cocoa\"\n"
#elif defined(CONFIG_VNC)
            "\"-vnc localhost:0,to=99,id=default\"\n"
#else
            "\"-display none\"\n"
#endif
    , QEMU_ARCH_ALL)

DEF("nographic", 0, QEMU_OPTION_nographic,
    "-nographic      disable graphical output and redirect serial I/Os to console\n",
    QEMU_ARCH_ALL)

DEF("full-screen", 0, QEMU_OPTION_full_screen,
    "-full-screen    start in full screen\n", QEMU_ARCH_ALL)

#ifdef CONFIG_VNC
DEF("vnc", HAS_ARG, QEMU_OPTION_vnc ,
    "-vnc <display>  shorthand for -display vnc=<display>\n", QEMU_ARCH_ALL)
#endif

DEFHEADING(Network options:)

DEF("netdev", HAS_ARG, QEMU_OPTION_netdev,
#ifdef CONFIG_SLIRP
    "-netdev user,id=str[,ipv4=on|off][,net=addr[/mask]][,host=addr]\n"
    "         [,ipv6=on|off][,ipv6-net=addr[/int]][,ipv6-host=addr]\n"
    "         [,restrict=on|off][,hostname=host][,dhcpstart=addr]\n"
    "         [,dns=addr][,ipv6-dns=addr][,dnssearch=domain][,domainname=domain]\n"
    "         [,tftp=dir][,tftp-server-name=name][,bootfile=f][,hostfwd=rule][,guestfwd=rule]"
#ifndef _WIN32
                                             "[,smb=dir[,smbserver=addr]]\n"
#endif
    "                configure a user mode network backend with ID 'str',\n"
    "                its DHCP server and optional services\n"
#endif
#ifdef _WIN32
    "-netdev tap,id=str,ifname=name\n"
    "                configure a host TAP network backend with ID 'str'\n"
#else
    "-netdev tap,id=str[,fd=h][,fds=x:y:...:z][,ifname=name][,script=file][,downscript=dfile]\n"
    "         [,br=bridge][,helper=helper][,sndbuf=nbytes][,vnet_hdr=on|off][,vhost=on|off]\n"
    "         [,vhostfd=h][,vhostfds=x:y:...:z][,vhostforce=on|off][,queues=n]\n"
    "         [,poll-us=n]\n"
    "                configure a host TAP network backend with ID 'str'\n"
    "                connected to a bridge (default=" DEFAULT_BRIDGE_INTERFACE ")\n"
    "                use network scripts 'file' (default=" DEFAULT_NETWORK_SCRIPT ")\n"
    "                to configure it and 'dfile' (default=" DEFAULT_NETWORK_DOWN_SCRIPT ")\n"
    "                to deconfigure it\n"
    "                use '[down]script=no' to disable script execution\n"
    "                use network helper 'helper' (default=" DEFAULT_BRIDGE_HELPER ") to\n"
    "                configure it\n"
    "                use 'fd=h' to connect to an already opened TAP interface\n"
    "                use 'fds=x:y:...:z' to connect to already opened multiqueue capable TAP interfaces\n"
    "                use 'sndbuf=nbytes' to limit the size of the send buffer (the\n"
    "                default is disabled 'sndbuf=0' to enable flow control set 'sndbuf=1048576')\n"
    "                use vnet_hdr=off to avoid enabling the IFF_VNET_HDR tap flag\n"
    "                use vnet_hdr=on to make the lack of IFF_VNET_HDR support an error condition\n"
    "                use vhost=on to enable experimental in kernel accelerator\n"
    "                    (only has effect for virtio guests which use MSIX)\n"
    "                use vhostforce=on to force vhost on for non-MSIX virtio guests\n"
    "                use 'vhostfd=h' to connect to an already opened vhost net device\n"
    "                use 'vhostfds=x:y:...:z to connect to multiple already opened vhost net devices\n"
    "                use 'queues=n' to specify the number of queues to be created for multiqueue TAP\n"
    "                use 'poll-us=n' to specify the maximum number of microseconds that could be\n"
    "                spent on busy polling for vhost net\n"
    "-netdev bridge,id=str[,br=bridge][,helper=helper]\n"
    "                configure a host TAP network backend with ID 'str' that is\n"
    "                connected to a bridge (default=" DEFAULT_BRIDGE_INTERFACE ")\n"
    "                using the program 'helper (default=" DEFAULT_BRIDGE_HELPER ")\n"
#endif
#ifdef __linux__
    "-netdev l2tpv3,id=str,src=srcaddr,dst=dstaddr[,srcport=srcport][,dstport=dstport]\n"
    "         [,rxsession=rxsession],txsession=txsession[,ipv6=on|off][,udp=on|off]\n"
    "         [,cookie64=on|off][,counter][,pincounter][,txcookie=txcookie]\n"
    "         [,rxcookie=rxcookie][,offset=offset]\n"
    "                configure a network backend with ID 'str' connected to\n"
    "                an Ethernet over L2TPv3 pseudowire.\n"
    "                Linux kernel 3.3+ as well as most routers can talk\n"
    "                L2TPv3. This transport allows connecting a VM to a VM,\n"
    "                VM to a router and even VM to Host. It is a nearly-universal\n"
    "                standard (RFC3931). Note - this implementation uses static\n"
    "                pre-configured tunnels (same as the Linux kernel).\n"
    "                use 'src=' to specify source address\n"
    "                use 'dst=' to specify destination address\n"
    "                use 'udp=on' to specify udp encapsulation\n"
    "                use 'srcport=' to specify source udp port\n"
    "                use 'dstport=' to specify destination udp port\n"
    "                use 'ipv6=on' to force v6\n"
    "                L2TPv3 uses cookies to prevent misconfiguration as\n"
    "                well as a weak security measure\n"
    "                use 'rxcookie=0x012345678' to specify a rxcookie\n"
    "                use 'txcookie=0x012345678' to specify a txcookie\n"
    "                use 'cookie64=on' to set cookie size to 64 bit, otherwise 32\n"
    "                use 'counter=off' to force a 'cut-down' L2TPv3 with no counter\n"
    "                use 'pincounter=on' to work around broken counter handling in peer\n"
    "                use 'offset=X' to add an extra offset between header and data\n"
#endif
    "-netdev socket,id=str[,fd=h][,listen=[host]:port][,connect=host:port]\n"
    "                configure a network backend to connect to another network\n"
    "                using a socket connection\n"
    "-netdev socket,id=str[,fd=h][,mcast=maddr:port[,localaddr=addr]]\n"
    "                configure a network backend to connect to a multicast maddr and port\n"
    "                use 'localaddr=addr' to specify the host address to send packets from\n"
    "-netdev socket,id=str[,fd=h][,udp=host:port][,localaddr=host:port]\n"
    "                configure a network backend to connect to another network\n"
    "                using an UDP tunnel\n"
    "-netdev stream,id=str[,server=on|off],addr.type=inet,addr.host=host,addr.port=port[,to=maxport][,numeric=on|off][,keep-alive=on|off][,mptcp=on|off][,addr.ipv4=on|off][,addr.ipv6=on|off][,reconnect-ms=milliseconds]\n"
    "-netdev stream,id=str[,server=on|off],addr.type=unix,addr.path=path[,abstract=on|off][,tight=on|off][,reconnect-ms=milliseconds]\n"
    "-netdev stream,id=str[,server=on|off],addr.type=fd,addr.str=file-descriptor[,reconnect-ms=milliseconds]\n"
    "                configure a network backend to connect to another network\n"
    "                using a socket connection in stream mode.\n"
    "-netdev dgram,id=str,remote.type=inet,remote.host=maddr,remote.port=port[,local.type=inet,local.host=addr]\n"
    "-netdev dgram,id=str,remote.type=inet,remote.host=maddr,remote.port=port[,local.type=fd,local.str=file-descriptor]\n"
    "                configure a network backend to connect to a multicast maddr and port\n"
    "                use ``local.host=addr`` to specify the host address to send packets from\n"
    "-netdev dgram,id=str,local.type=inet,local.host=addr,local.port=port[,remote.type=inet,remote.host=addr,remote.port=port]\n"
    "-netdev dgram,id=str,local.type=unix,local.path=path[,remote.type=unix,remote.path=path]\n"
    "-netdev dgram,id=str,local.type=fd,local.str=file-descriptor\n"
    "                configure a network backend to connect to another network\n"
    "                using an UDP tunnel\n"
#ifdef CONFIG_VDE
    "-netdev vde,id=str[,sock=socketpath][,port=n][,group=groupname][,mode=octalmode]\n"
    "                configure a network backend to connect to port 'n' of a vde switch\n"
    "                running on host and listening for incoming connections on 'socketpath'.\n"
    "                Use group 'groupname' and mode 'octalmode' to change default\n"
    "                ownership and permissions for communication port.\n"
#endif
#ifdef CONFIG_NETMAP
    "-netdev netmap,id=str,ifname=name[,devname=nmname]\n"
    "                attach to the existing netmap-enabled network interface 'name', or to a\n"
    "                VALE port (created on the fly) called 'name' ('nmname' is name of the \n"
    "                netmap device, defaults to '/dev/netmap')\n"
#endif
#ifdef CONFIG_VMNET
    "-netdev vmnet-host,id=str[,isolated=on|off][,net-uuid=uuid]\n"
    "         [,start-address=addr,end-address=addr,subnet-mask=mask]\n"
    "                configure a vmnet network backend in host mode with ID 'str',\n"
    "                isolate this interface from others with 'isolated',\n"
    "                configure the address range and choose a subnet mask,\n"
    "                specify network UUID 'uuid' to disable DHCP and interact with\n"
    "                vmnet-host interfaces within this isolated network\n"
    "-netdev vmnet-shared,id=str[,isolated=on|off][,nat66-prefix=addr]\n"
    "         [,start-address=addr,end-address=addr,subnet-mask=mask]\n"
    "                configure a vmnet network backend in shared mode with ID 'str',\n"
    "                configure the address range and choose a subnet mask,\n"
    "                set IPv6 ULA prefix (of length 64) to use for internal network,\n"
    "                isolate this interface from others with 'isolated'\n"
    "-netdev vmnet-bridged,id=str,ifname=name[,isolated=on|off]\n"
    "                configure a vmnet network backend in bridged mode with ID 'str',\n"
    "                use 'ifname=name' to select a physical network interface to be bridged,\n"
    "                isolate this interface from others with 'isolated'\n"
#endif
    "-netdev hubport,id=str,hubid=n[,netdev=nd]\n"
    "                configure a hub port on the hub with ID 'n'\n", QEMU_ARCH_ALL)
DEF("nic", HAS_ARG, QEMU_OPTION_nic,
    "-nic [tap|bridge|"
#ifdef CONFIG_SLIRP
    "user|"
#endif
#ifdef __linux__
    "l2tpv3|"
#endif
#ifdef CONFIG_VDE
    "vde|"
#endif
#ifdef CONFIG_NETMAP
    "netmap|"
#endif
#ifdef CONFIG_VMNET
    "vmnet-host|vmnet-shared|vmnet-bridged|"
#endif
    "socket][,option][,...][mac=macaddr]\n"
    "                initialize an on-board / default host NIC (using MAC address\n"
    "                macaddr) and connect it to the given host network backend\n"
    "-nic none       use it alone to have zero network devices (the default is to\n"
    "                provided a 'user' network connection)\n",
    QEMU_ARCH_ALL)
DEF("net", HAS_ARG, QEMU_OPTION_net,
    "-net nic[,macaddr=mac][,model=type][,name=str][,addr=str][,vectors=v]\n"
    "                configure or create an on-board (or machine default) NIC and\n"
    "                connect it to hub 0 (please use -nic unless you need a hub)\n"
    "-net ["
#ifdef CONFIG_SLIRP
    "user|"
#endif
    "tap|"
    "bridge|"
#ifdef CONFIG_VDE
    "vde|"
#endif
#ifdef CONFIG_NETMAP
    "netmap|"
#endif
#ifdef CONFIG_VMNET
    "vmnet-host|vmnet-shared|vmnet-bridged|"
#endif
    "socket][,option][,option][,...]\n"
    "                old way to initialize a host network interface\n"
    "                (use the -netdev option if possible instead)\n", QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(Character device options:)

DEF("chardev", HAS_ARG, QEMU_OPTION_chardev,
    "-chardev help\n"
    "-chardev null,id=id[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev socket,id=id[,host=host],port=port[,to=to][,ipv4=on|off][,ipv6=on|off][,nodelay=on|off]\n"
    "         [,server=on|off][,wait=on|off][,telnet=on|off][,websocket=on|off][,reconnect-ms=milliseconds][,mux=on|off]\n"
    "         [,logfile=PATH][,logappend=on|off][,tls-creds=ID][,tls-authz=ID] (tcp)\n"
    "-chardev socket,id=id,path=path[,server=on|off][,wait=on|off][,telnet=on|off][,websocket=on|off][,reconnect-ms=milliseconds]\n"
    "         [,mux=on|off][,logfile=PATH][,logappend=on|off][,abstract=on|off][,tight=on|off] (unix)\n"
    "-chardev udp,id=id[,host=host],port=port[,localaddr=localaddr]\n"
    "         [,localport=localport][,ipv4=on|off][,ipv6=on|off][,mux=on|off]\n"
    "         [,logfile=PATH][,logappend=on|off]\n"
    "-chardev msmouse,id=id[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev vc,id=id[[,width=width][,height=height]][[,cols=cols][,rows=rows]]\n"
    "         [,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev ringbuf,id=id[,size=size][,logfile=PATH][,logappend=on|off]\n"
    "-chardev file,id=id,path=path[,input-path=input-file][,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev pipe,id=id,path=path[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
#ifdef _WIN32
    "-chardev console,id=id[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev serial,id=id,path=path[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
#else
    "-chardev pty,id=id[,path=path][,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
    "-chardev stdio,id=id[,mux=on|off][,signal=on|off][,logfile=PATH][,logappend=on|off]\n"
#endif
#ifdef CONFIG_BRLAPI
    "-chardev braille,id=id[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
#endif
#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
        || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    "-chardev serial,id=id,path=path[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
#endif
#if defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
    "-chardev parallel,id=id,path=path[,mux=on|off][,logfile=PATH][,logappend=on|off]\n"
#endif
    , QEMU_ARCH_ALL
)


DEFHEADING()

DEFHEADING(Boot Image or Kernel specific:)


DEF("bios", HAS_ARG, QEMU_OPTION_bios, \
    "-bios file      set the filename for the BIOS\n", QEMU_ARCH_ALL)

DEF("pflash", HAS_ARG, QEMU_OPTION_pflash,
    "-pflash file    use 'file' as a parallel flash image\n", QEMU_ARCH_ALL)


DEF("kernel", HAS_ARG, QEMU_OPTION_kernel, \
    "-kernel bzImage use 'bzImage' as kernel image\n", QEMU_ARCH_ALL)

DEF("shim", HAS_ARG, QEMU_OPTION_shim, \
    "-shim shim.efi use 'shim.efi' to boot the kernel\n", QEMU_ARCH_ALL)

DEF("append", HAS_ARG, QEMU_OPTION_append, \
    "-append cmdline use 'cmdline' as kernel command line\n", QEMU_ARCH_ALL)

DEF("initrd", HAS_ARG, QEMU_OPTION_initrd, \
           "-initrd file    use 'file' as initial ram disk\n", QEMU_ARCH_ALL)

DEF("dtb", HAS_ARG, QEMU_OPTION_dtb, \
    "-dtb    file    use 'file' as device tree image\n", QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(Debug/Expert options:)

DEF("compat", HAS_ARG, QEMU_OPTION_compat,
    "-compat [deprecated-input=accept|reject|crash][,deprecated-output=accept|hide]\n"
    "                Policy for handling deprecated management interfaces\n"
    "-compat [unstable-input=accept|reject|crash][,unstable-output=accept|hide]\n"
    "                Policy for handling unstable management interfaces\n",
    QEMU_ARCH_ALL)

DEF("fw_cfg", HAS_ARG, QEMU_OPTION_fwcfg,
    "-fw_cfg [name=]<name>,file=<file>\n"
    "                add named fw_cfg entry with contents from file\n"
    "-fw_cfg [name=]<name>,string=<str>\n"
    "                add named fw_cfg entry with contents from string\n",
    QEMU_ARCH_ALL)

DEF("serial", HAS_ARG, QEMU_OPTION_serial, \
    "-serial dev     redirect the serial port to char device 'dev'\n",
    QEMU_ARCH_ALL)

DEF("parallel", HAS_ARG, QEMU_OPTION_parallel, \
    "-parallel dev   redirect the parallel port to char device 'dev'\n",
    QEMU_ARCH_ALL)

DEF("monitor", HAS_ARG, QEMU_OPTION_monitor, \
    "-monitor dev    redirect the monitor to char device 'dev'\n",
    QEMU_ARCH_ALL)
DEF("qmp", HAS_ARG, QEMU_OPTION_qmp, \
    "-qmp dev        like -monitor but opens in 'control' mode\n",
    QEMU_ARCH_ALL)
DEF("qmp-pretty", HAS_ARG, QEMU_OPTION_qmp_pretty, \
    "-qmp-pretty dev like -qmp but uses pretty JSON formatting\n",
    QEMU_ARCH_ALL)

DEF("mon", HAS_ARG, QEMU_OPTION_mon, \
    "-mon [chardev=]name[,mode=readline|control][,pretty[=on|off]]\n", QEMU_ARCH_ALL)

DEF("debugcon", HAS_ARG, QEMU_OPTION_debugcon, \
    "-debugcon dev   redirect the debug console to char device 'dev'\n",
    QEMU_ARCH_ALL)

DEF("pidfile", HAS_ARG, QEMU_OPTION_pidfile, \
    "-pidfile file   write PID to 'file'\n", QEMU_ARCH_ALL)

DEF("preconfig", 0, QEMU_OPTION_preconfig, \
    "--preconfig     pause QEMU before machine is initialized (experimental)\n",
    QEMU_ARCH_ALL)

DEF("S", 0, QEMU_OPTION_S, \
    "-S              freeze CPU at startup (use 'c' to start execution)\n",
    QEMU_ARCH_ALL)

DEF("overcommit", HAS_ARG, QEMU_OPTION_overcommit,
    "-overcommit [mem-lock=on|off|on-fault][cpu-pm=on|off]\n"
    "                run qemu with overcommit hints\n"
    "                mem-lock=on|off|on-fault controls memory lock support (default: off)\n"
    "                cpu-pm=on|off controls cpu power management (default: off)\n",
    QEMU_ARCH_ALL)

DEF("gdb", HAS_ARG, QEMU_OPTION_gdb, \
    "-gdb dev        accept gdb connection on 'dev'. (QEMU defaults to starting\n"
    "                the guest without waiting for gdb to connect; use -S too\n"
    "                if you want it to not start execution.)\n",
    QEMU_ARCH_ALL)

DEF("s", 0, QEMU_OPTION_s, \
    "-s              shorthand for -gdb tcp::" DEFAULT_GDBSTUB_PORT "\n",
    QEMU_ARCH_ALL)

DEF("d", HAS_ARG, QEMU_OPTION_d, \
    "-d item1,...    enable logging of specified items (use '-d help' for a list of log items)\n",
    QEMU_ARCH_ALL)

DEF("D", HAS_ARG, QEMU_OPTION_D, \
    "-D logfile      output log to logfile (default stderr)\n",
    QEMU_ARCH_ALL)

DEF("dfilter", HAS_ARG, QEMU_OPTION_DFILTER, \
    "-dfilter range,..  filter debug output to range of addresses (useful for -d cpu,exec,etc..)\n",
    QEMU_ARCH_ALL)

DEF("seed", HAS_ARG, QEMU_OPTION_seed, \
    "-seed number       seed the pseudo-random number generator\n",
    QEMU_ARCH_ALL)

DEF("L", HAS_ARG, QEMU_OPTION_L, \
    "-L path         set the directory for the BIOS, VGA BIOS and keymaps\n",
    QEMU_ARCH_ALL)

DEF("enable-kvm", 0, QEMU_OPTION_enable_kvm, \
    "-enable-kvm     enable KVM full virtualization support\n",
    QEMU_ARCH_ARM)

DEF("no-reboot", 0, QEMU_OPTION_no_reboot, \
    "-no-reboot      exit instead of rebooting\n", QEMU_ARCH_ALL)

DEF("no-shutdown", 0, QEMU_OPTION_no_shutdown, \
    "-no-shutdown    stop before shutdown\n", QEMU_ARCH_ALL)

DEF("action", HAS_ARG, QEMU_OPTION_action,
    "-action reboot=reset|shutdown\n"
    "                   action when guest reboots [default=reset]\n"
    "-action shutdown=poweroff|pause\n"
    "                   action when guest shuts down [default=poweroff]\n"
    "-action panic=pause|shutdown|exit-failure|none\n"
    "                   action when guest panics [default=shutdown]\n"
    "-action watchdog=reset|shutdown|poweroff|inject-nmi|pause|debug|none\n"
    "                   action when watchdog fires [default=reset]\n",
    QEMU_ARCH_ALL)

DEF("loadvm", HAS_ARG, QEMU_OPTION_loadvm, \
    "-loadvm [tag|id]\n" \
    "                start right away with a saved state (loadvm in monitor)\n",
    QEMU_ARCH_ALL)

#if !defined(_WIN32) && !defined(EMSCRIPTEN)
DEF("daemonize", 0, QEMU_OPTION_daemonize, \
    "-daemonize      daemonize QEMU after initializing\n", QEMU_ARCH_ALL)
#endif

DEF("rtc", HAS_ARG, QEMU_OPTION_rtc, \
    "-rtc [base=utc|localtime|<datetime>][,clock=host|rt|vm]\n" \
    "                set the RTC base and clock\n",
    QEMU_ARCH_ALL)


DEF("watchdog-action", HAS_ARG, QEMU_OPTION_watchdog_action, \
    "-watchdog-action reset|shutdown|poweroff|inject-nmi|pause|debug|none\n" \
    "                action when watchdog fires [default=reset]\n",
    QEMU_ARCH_ALL)

DEF("echr", HAS_ARG, QEMU_OPTION_echr, \
    "-echr chr       set terminal escape character instead of ctrl-a\n",
    QEMU_ARCH_ALL)


DEF("sandbox", HAS_ARG, QEMU_OPTION_sandbox, \
    "-sandbox on[,obsolete=allow|deny][,elevateprivileges=allow|deny|children]\n" \
    "          [,spawn=allow|deny][,resourcecontrol=allow|deny]\n" \
    "                Enable seccomp mode 2 system call filter (default 'off').\n" \
    "                use 'obsolete' to allow obsolete system calls that are provided\n" \
    "                    by the kernel, but typically no longer used by modern\n" \
    "                    C library implementations.\n" \
    "                use 'elevateprivileges' to allow or deny the QEMU process ability\n" \
    "                    to elevate privileges using set*uid|gid system calls.\n" \
    "                    The value 'children' will deny set*uid|gid system calls for\n" \
    "                    main QEMU process but will allow forks and execves to run unprivileged\n" \
    "                use 'spawn' to avoid QEMU to spawn new threads or processes by\n" \
    "                     blocking *fork and execve\n" \
    "                use 'resourcecontrol' to disable process affinity and schedular priority\n",
    QEMU_ARCH_ALL)

DEF("readconfig", HAS_ARG, QEMU_OPTION_readconfig,
    "-readconfig <file>\n"
    "                read config file\n", QEMU_ARCH_ALL)

DEF("no-user-config", 0, QEMU_OPTION_nouserconfig,
    "-no-user-config\n"
    "                do not load default user-provided config files at startup\n",
    QEMU_ARCH_ALL)

DEF("trace", HAS_ARG, QEMU_OPTION_trace,
    "-trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
    "                specify tracing options\n",
    QEMU_ARCH_ALL)

#if defined(CONFIG_POSIX) && !defined(EMSCRIPTEN)
DEF("run-with", HAS_ARG, QEMU_OPTION_run_with,
    "-run-with [async-teardown=on|off][,chroot=dir][user=username|uid:gid]\n"
    "                Set miscellaneous QEMU process lifecycle options:\n"
    "                async-teardown=on enables asynchronous teardown (Linux only)\n"
    "                chroot=dir chroot to dir just before starting the VM\n"
    "                user=username switch to the specified user before starting the VM\n"
    "                user=uid:gid ditto, but use specified user-ID and group-ID instead\n",
    QEMU_ARCH_ALL)
#endif

DEF("msg", HAS_ARG, QEMU_OPTION_msg,
    "-msg [timestamp[=on|off]][,guest-name=[on|off]]\n"
    "                control error message format\n"
    "                timestamp=on enables timestamps (default: off)\n"
    "                guest-name=on enables guest name prefix but only if\n"
    "                              -name guest option is set (default: off)\n",
    QEMU_ARCH_ALL)

DEF("enable-sync-profile", 0, QEMU_OPTION_enable_sync_profile,
    "-enable-sync-profile\n"
    "                enable synchronization profiling\n",
    QEMU_ARCH_ALL)

DEFHEADING()

DEFHEADING(Generic object creation:)

DEF("object", HAS_ARG, QEMU_OPTION_object,
    "-object TYPENAME[,PROP1=VALUE1,...]\n"
    "                create a new object of type TYPENAME setting properties\n"
    "                in the order they are specified.  Note that the 'id'\n"
    "                property must be set.  These objects are placed in the\n"
    "                '/objects' path.\n",
    QEMU_ARCH_ALL)

HXCOMM This is the last statement. Insert new options before this line!

#undef DEF
#undef DEFHEADING
#undef ARCHHEADING
