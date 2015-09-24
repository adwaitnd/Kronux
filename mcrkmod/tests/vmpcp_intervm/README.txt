###############################################################################
Requirements for vMPCP Inter-VM mutex test:
 - Libvirt (version >= 0.9.8)
 - VCPUs must be attached to a host resource set with CPURSV_NO_MIGRATION
 - Tasks in guest must be attached to a guest resource set with CPURSV_NO_MIGRATION


###############################################################################
Test steps:

1. Host: setup RK options
 - Edit include/rk/rk_common.h
	#define RK_UNIQUE_PRIORITY_ACROSS_PROCESSORS
	#define RK_TRACE
	#define RK_EVENT_LOG
	#define RK_VIRT_SUPPORT

2. Host: setup per-vcpu virtqueue (virtio-serial) for a guest VM
 - Edit vm config:
	$ virsh edit ubuntu-vm1

	Add the following in the xml (ex, 4 VCPUs -> 4 virtio-serial controllers & channels):
	    ...
	    <controller type='virtio-serial' index='0'>
	      <address type='pci' domain='0x0000' bus='0x00' slot='0x07' function='0x0'/>
	    </controller>
	    <controller type='virtio-serial' index='1'>
	      <address type='pci' domain='0x0000' bus='0x00' slot='0x08' function='0x0'/>
	    </controller>
	    <controller type='virtio-serial' index='2'>
	      <address type='pci' domain='0x0000' bus='0x00' slot='0x09' function='0x0'/>
	    </controller>
	    <controller type='virtio-serial' index='3'>
	      <address type='pci' domain='0x0000' bus='0x00' slot='0x0a' function='0x0'/>
	    </controller>
	    ...
	    <channel type='unix'>
	      <source mode='bind' path='/tmp/rk-vm1.0'/>
	      <target type='virtio' name='rk-vchannel.0'/>
	      <address type='virtio-serial' controller='0' bus='0' port='1'/>
	    </channel>
	    <channel type='unix'>
	      <source mode='bind' path='/tmp/rk-vm1.1'/>
	      <target type='virtio' name='rk-vchannel.1'/>
	      <address type='virtio-serial' controller='1' bus='0' port='2'/>
	    </channel>
	    <channel type='unix'>
	      <source mode='bind' path='/tmp/rk-vm1.2'/>
	      <target type='virtio' name='rk-vchannel.2'/>
	      <address type='virtio-serial' controller='2' bus='0' port='3'/>
	    </channel>
	    <channel type='unix'>
	      <source mode='bind' path='/tmp/rk-vm1.3'/>
	      <target type='virtio' name='rk-vchannel.3'/>
	      <address type='virtio-serial' controller='3' bus='0' port='4'/>
	    </channel>
	    ...

3. Host: load the host RK module

4. Host: start the guest VM

5. Guest: kill irqbalance
	$ sudo killall irqbalance

6. Guest: set CPU affinities for virtio-serial interrupts
 - Check interrupts:
	$ cat /proc/interrupts
	           CPU0       CPU1       CPU2       CPU3       
	  0:         31          0          0          0   IO-APIC-edge      timer
	  1:       2229          0          0          0   IO-APIC-edge      i8042
	  6:          2          0          0          0   IO-APIC-edge      floppy
	  8:          1          0          0          0   IO-APIC-edge      rtc0
	  9:          0          0          0          0   IO-APIC-fasteoi   acpi
	 11:          0          0          0          0   IO-APIC-fasteoi   uhci_hcd:usb1, virtio2
	 12:        145        255          0          0   IO-APIC-edge      i8042
	 14:          0          0          0          0   IO-APIC-edge      ata_piix
	 15:       1051          0          0          0   IO-APIC-edge      ata_piix
	 40:          0          0          0          0   PCI-MSI-edge      virtio1-config
	 41:       3397          0          0          0   PCI-MSI-edge      virtio1-requests
	 42:          0          0          0          0   PCI-MSI-edge      virtio0-config
	 43:        576          0          0          0   PCI-MSI-edge      virtio0-input.0
	 44:          1          0          0          0   PCI-MSI-edge      virtio0-output.0
	 45:          0          0          0          0   PCI-MSI-edge      virtio3-config
	 46:         22          0          0          0   PCI-MSI-edge      virtio3-virtqueues
	 47:          0          0          0          0   PCI-MSI-edge      virtio4-config
	 48:         16          5          0          0   PCI-MSI-edge      virtio4-virtqueues
	 49:          0          0          0          0   PCI-MSI-edge      virtio5-config
	 50:         23          0          5          0   PCI-MSI-edge      virtio5-virtqueues
	 51:          0          0          0          0   PCI-MSI-edge      virtio6-config
	 52:         19          0          0          0   PCI-MSI-edge      virtio6-virtqueues
 - Set CPU affinity for virtio-serial (virtqueues):
	$ sudo su
	# echo 0 > /proc/irq/46/smp_affinity_list 
	# echo 1 > /proc/irq/48/smp_affinity_list
	# echo 2 > /proc/irq/50/smp_affinity_list
	# echo 3 > /proc/irq/52/smp_affinity_list
	
	or 
	
	# echo 1 > /proc/irq/46/smp_affinity
	# echo 2 > /proc/irq/48/smp_affinity
	# echo 4 > /proc/irq/50/smp_affinity
	# echo 8 > /proc/irq/52/smp_affinity
 - Do this for both VMs

7. Guest: load guest RK module
 - Dmesg and check if the guest RK understands that it is running in a VM
        [  530.496877] RK running in a virtual machine

8. Host: create a resource set for guest VM
 - cd mcrkmod/tests/vmpcp_intervm
 - Create a resource set: (ex, four VCPUs for each of two VMs)
	$ ./create_cpursvs
	RSET-VM1: Resource Set Descriptor is 0
	RSET-VM2: Resource Set Descriptor is 1
 - Check VCPU pids: (first four child threads)
	$ ps -eLfc
	...
	117      12088     1 12088    7 TS   19 11:25 ?        00:00:14 /usr/bin/kvm ... 
	117      12088     1 12091    7 TS   19 11:25 ?        00:01:09 /usr/bin/kvm ... 
	117      12088     1 12092    7 TS   19 11:25 ?        00:00:06 /usr/bin/kvm ... 
	117      12088     1 12093    7 TS   19 11:25 ?        00:00:06 /usr/bin/kvm ... 
	117      12088     1 12094    7 TS   19 11:25 ?        00:00:07 /usr/bin/kvm ... 
	...
	117      12193     1 12193    5 TS   19 11:25 ?        00:00:12 /usr/bin/kvm ... 
	117      12193     1 12196    5 TS   19 11:25 ?        00:00:35 /usr/bin/kvm ... 
	117      12193     1 12197    5 TS   19 11:25 ?        00:00:05 /usr/bin/kvm ... 
	117      12193     1 12198    5 TS   19 11:25 ?        00:00:06 /usr/bin/kvm ... 
	117      12193     1 12199    5 TS   19 11:25 ?        00:00:06 /usr/bin/kvm ... 
	...
 - Attach VCPUs to the resource set:
	$ ./attach_vcpus 0 12091 12092 12093 12094
	VCPU Pid  : 12091	 -> CPURSV 3 of RD 0
	VCPU Pid  : 12092	 -> CPURSV 2 of RD 0
	VCPU Pid  : 12093	 -> CPURSV 1 of RD 0
	VCPU Pid  : 12094	 -> CPURSV 0 of RD 0

	$ ./attach_vcpus 1 12196 12197 12198 12199
	VCPU Pid  : 12196	 -> CPURSV 3 of RD 1
	VCPU Pid  : 12197	 -> CPURSV 2 of RD 1
	VCPU Pid  : 12198	 -> CPURSV 1 of RD 1
	VCPU Pid  : 12199	 -> CPURSV 0 of RD 1

9. (optional) Host: set RT prio for QEMU task (needs sudo)
	$ cd mcrkmod/tests/vmpcp_intervm/
	$ sudo ./set_rt_prio 12088
	$ sudo ./set_rt_prio 12193

10. Host: set event log for guest VCPUs 
	$ cd mcrkmod/utils/
	$ ./rk_event_log_set 12091 
	$ ./rk_event_log_set 12092 
	$ ./rk_event_log_set 12093 
	$ ./rk_event_log_set 12094 

	$ ./rk_event_log_set 12196 
	$ ./rk_event_log_set 12197 
	$ ./rk_event_log_set 12198 
	$ ./rk_event_log_set 12199 

11. Host: register vchannel for guest VM (needs sudo) 
 - Note) For vmpcp_intervm test, use reg_vchannel_host, instead of 
         rk_vchannel_register_host in mcrkmod/utils/virt.

	$ cd mcrkmod/tests/vmpcp_intervm
	$ sudo ./reg_vchannel_host 0 /tmp/rk-vm1
	$ sudo ./reg_vchannel_host 1 /tmp/rk-vm2

12. Guest: register vchannel for itself (needs sudo)
	$ cd mcrkmod/utils/virt
	$ sudo ./rk_vchannel_register_guest 4 /dev/virtio-ports/rk-vchannel
	  (Note) 4: number of vcpus
	         /tmp/rk-vm1: path to guest vchannel (virtio-serial)


13. (optional) Check if vchannel messages are correctly delivered
 - Guest:
        $ cat /proc/interrupts
	...
	52:         19          0          0          0   PCI-MSI-edge      virtio6-virtqueues
	...
	(Zero interrupt count for VCPU3)

 - Host: 
	$ cd mcrkmod/utils/virt
	$ ./rk_vchannel_send_cmd 0 0 123 456   
	  -->  Send rk-vchannel msg (123,456) to the VCPU3 (cpursv:0) VM1(rset:0)
 - Guest:
 	$ cat /proc/interrupts
	...
	52:         19          0          0          1   PCI-MSI-edge      virtio6-virtqueues
	...
	(One interrupt delivered to VCPU3)

14. Guest: configure vmpcp options in the test program
 - In VM1 and VM2:
	$ cd mcrkmod/tests/vmpcp_intervm
	$ vi tasks_for_vm.c

	(with no overrun)
	mid = rk_vmpcp_intervm_mutex_open(key, MTX_CREATE);
	
	(with overrun)
	mid = rk_vmpcp_intervm_mutex_open(key, MTX_CREATE | MTX_OVERRUN);

	$ make

15. Host: start getting event logs (log_rk_event.txt will be created)
	$ cd mcrkmod/utils/
	$ ./rk_event_log_get 1

16. Guest: run tasks_for_vm1/2
 - VM1:
	$ cd mcrkmod/tests/vmpcp_intervm
	$ ./tasks_for_vm1
	VM1: pid 2778... waiting for host

 - VM2:
	$ cd mcrkmod/tests/vmpcp_intervm
	$ ./tasks_for_vm2
	VM2: pid 2985... waiting for host

17. Host: send start signals to tasks_for_vm1/2 in guest VMs
	$ cd mcrkmod/tests/vmpcp_intervm
	$ ./wakeup_vm_tasks 2778 2985

 - In guest VMs, tasks_for_vm1/2 will print out PIDs of four tasks being tested.
 - After a few seconds, stop task_for_vm1/2 in the guests and rk_event_log_get in the host.
   Open mcrkmod/utils/log_rk_event.txt in the host to check task execution history.

 - In log_rk_event.txt, each line shows a logged event in the following format:
   <time_sec.time_nsec>, <event_type>, <cpuid>, <pid>, <arg1>, <arg2>

   The <event_type> field represents the type of a logged event. Descriptions on each event
   type are as follows:
   
   <event_type> = 0: Start/resumption of VCPU
   <event_type> = 1: Stop/suspension of VCPU
      - <pid> is the pid of the monitored VCPU task
      - <cpuid> is the physical CPU ID where the VCPU <pid> is running at <time_sec.time_nsec>
      - <arg1> is unused
      - <arg2> is the priority of the VCPU <pid> at that time
   
   <event_type> = 100: Start/resumption of Task in VM
   <event_type> = 101: Stop/suspension of Task in VM
   <event_type> = 102: Start the global critical section of Task in VM
   <event_type> = 103: Finish the global critical section of Task in VM
      - <pid> is the pid of the VCPU where the monitored task is running
      - <cpuid> is the physical CPU ID where the VCPU <pid> is running
      - <arg1> is the pid of the monitored task running on the VCPU <pid>
      - <arg2> is the priority of the VCPU <pid> at that time.

 - The first event of each task running in a VM is "<event_type> = 101". As our interest 
   is in the execution history of those tasks running in VMs, any event recorded before
   the first event with the event_type of 101 can be ignored. All the tasks in vmpcp_intervm
   are released exactly 1 second after the first event with the event_type of 101. 
   
 - For VCPU and task parameters, please refer to the source code of vmpcp_intervm.

18. Host/Guest: unload RK module
 - When all test is done, you can unload RK modules.
 - Note that "sudo rmmod rk" in the guest may be stalled until the host RK module is unloaded.

