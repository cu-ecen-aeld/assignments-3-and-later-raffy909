# Analysis of the assignemnt 7 kernel oops
The kernel oops in question is presented when the following command get runned in the qemu terminal for the assignemnt:

	#echo “hello_world” > /dev/faulty

As the result of the command execution we get the current kernel error:

	Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
	Mem abort info:
	ESR = 0x96000045
	EC = 0x25: DABT (current EL), IL = 32 bits
	SET = 0, FnV = 0
	EA = 0, S1PTW = 0
	FSC = 0x05: level 1 translation fault
	Data abort info:
	ISV = 0, ISS = 0x00000045
	CM = 0, WnR = 1
	user pgtable: 4k pages, 39-bit VAs, pgdp=0000000042677000
	[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
	Internal error: Oops: 96000045 [#1] SMP
	Modules linked in: faulty(O) scull(O) hello(O)
	CPU: 0 PID: 160 Comm: sh Tainted: G O 5.15.18 #1
	Hardware name: linux,dummy-virt (DT)
	pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
	pc : faulty_write+0x14/0x20 [faulty]
	lr : vfs_write+0xa8/0x2b0
	sp : ffffffc008c93d80
	x29: ffffffc008c93d80 x28: ffffff800268d940 x27: 0000000000000000
	x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
	x23: 0000000040000000 x22: 0000000000000012 x21: 000000556def26c0
	x20: 000000556def26c0 x19: ffffff8002676400 x18: 0000000000000000
	x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
	x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
	x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
	x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
	x5 : 0000000000000001 x4 : ffffffc0006fc000 x3 : ffffffc008c93df0
	x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000

	Call trace:
	 faulty_write+0x14/0x20 [faulty]
	 ksys_write+0x68/0x100
	 __arm64_sys_write+0x20/0x30
	 invoke_syscall+0x54/0x130
	 el0_svc_common.constprop.0+0x44/0xf0
	 do_el0_svc+0x40/0xa0
	 el0_svc+0x20/0x60
	 el0t_64_sync_handler+0xe8/0xf0
	 el0t_64_sync+0x1a0/0x1a4
	 Code: d2800001 d2800000 d503233f d50323bf (b900003f)
	---[ end trace 301175e03dceeefc ]---

From the intitial analysis of the rerror we can get a very usefull hint by the kernel wich is:

>Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000

The error is triggered by a NULL pointer dereference at virtual address `0x0000000000000000`. This occurs in the `faulty_write` function of the `faulty` kernel module, as indicated by the program counter (`pc`) value at the time of the crash, more specifically the line`pc : faulty_write+0x14/0x20` tells us that the error occurred at a specific point within the `faulty_write` function of the `faulty` module and to be more specific the exact location is 20 (0x14) bytes after the start of the function.
Other useful information can be obtained from the call trace:

	Call trace:
	 faulty_write+0x14/0x20 [faulty]
	 ksys_write+0x68/0x100
	 __arm64_sys_write+0x20/0x30
	 invoke_syscall+0x54/0x130
	 el0_svc_common.constprop.0+0x44/0xf0
	 do_el0_svc+0x40/0xa0
	 el0_svc+0x20/0x60
	 el0t_64_sync_handler+0xe8/0xf0
	 el0t_64_sync+0x1a0/0x1a4

From the call trace we can observe that immediately after the line `faulty_write+0x14/0x20 [faulty]` there are three lines that are a pretty clear indication thet the error was triggered after a write operation to the driver, the lines in question are:
- ` ksys_write+0x68/0x100`
- `__arm64_sys_write+0x20/0x30`
- `invoke_syscall+0x54/0x130`