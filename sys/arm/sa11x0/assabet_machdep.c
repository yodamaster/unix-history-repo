/*	$NetBSD: hpc_machdep.c,v 1.70 2003/09/16 08:18:22 agc Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * machdep.c
 *
 * Machine dependant functions for kernel setup
 *
 * This file needs a lot of work. 
 *
 * Created      : 17/09/94
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _ARM32_BUS_DMA_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/cons.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/buf.h>
#include <sys/exec.h>
#include <machine/reg.h>
#include <machine/cpu.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <vm/vnode_pager.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/pcb.h>
#include <machine/undefined.h>
#include <machine/machdep.h>
#include <machine/metadata.h>
#include <machine/armreg.h>
#include <machine/bus.h>
#include <sys/reboot.h>

#define MDROOT_ADDR 0xd0400000

#define KERNEL_PT_VMEM		0	/* Page table for mapping video memory */
#define KERNEL_PT_SYS		0	/* Page table for mapping proc0 zero page */
#define KERNEL_PT_IO		3	/* Page table for mapping IO */
#define KERNEL_PT_IRQ		2	/* Page table for mapping irq handler */
#define KERNEL_PT_KERNEL	1	/* Page table for mapping kernel */
#define	KERNEL_PT_VMDATA	4	/* Page tables for mapping kernel VM */
#define	KERNEL_PT_VMDATA_NUM	12	/* start with 16MB of KVM */
#define	NUM_KERNEL_PTS		(KERNEL_PT_VMDATA + KERNEL_PT_VMDATA_NUM)

/* Define various stack sizes in pages */
#define IRQ_STACK_SIZE	1
#define ABT_STACK_SIZE	1
#ifdef IPKDB
#define UND_STACK_SIZE	2
#else
#define UND_STACK_SIZE	1
#endif
#define	KERNEL_VM_BASE		(KERNBASE + 0x00c00000)
#define	KERNEL_VM_SIZE		0x05000000

extern u_int data_abort_handler_address;
extern u_int prefetch_abort_handler_address;
extern u_int undefined_handler_address;

struct pv_addr kernel_pt_table[NUM_KERNEL_PTS];

extern void *_end;

int got_mmu = 0;

extern vm_offset_t sa1_cache_clean_addr;

extern int *end;

struct pcpu __pcpu;
struct pcpu *pcpup = &__pcpu;

#define MDSIZE 8192
/* Physical and virtual addresses for some global pages */

vm_paddr_t phys_avail[10];
vm_paddr_t physical_start;
vm_paddr_t physical_end;
vm_paddr_t physical_freestart;
vm_offset_t physical_pages;
vm_offset_t clean_sva, clean_eva;

struct pv_addr systempage;
struct pv_addr irqstack;
struct pv_addr undstack;
struct pv_addr abtstack;
struct pv_addr kernelstack;
void enable_mmu(vm_offset_t);
static struct trapframe proc0_tf;

struct arm32_dma_range *
bus_dma_get_range(void)
{

	return (NULL);
}

#define CPU_SA110_CACHE_CLEAN_SIZE (0x4000 * 2)

void *
initarm(void *arg, void *arg2)
{
	struct pcpu *pc;
	struct pv_addr  kernel_l1pt;
	struct pv_addr	proc0_uarea;
	struct pv_addr	md_addr;
	struct pv_addr	md_bla;
	int loop;
	u_int kerneldatasize, symbolsize;
	u_int l1pagetable;
	vm_offset_t freemempos;
	vm_size_t pt_size;
	int i = 0;
	uint32_t fake_preload[35];

	boothowto = RB_VERBOSE | RB_SINGLE;
	cninit();
	set_cpufuncs();
	fake_preload[i++] = MODINFO_NAME;
	fake_preload[i++] = strlen("elf kernel") + 1;
	strcpy((char*)&fake_preload[i++], "elf kernel");
	i += 2;
	fake_preload[i++] = MODINFO_TYPE;
	fake_preload[i++] = strlen("elf kernel") + 1;
	strcpy((char*)&fake_preload[i++], "elf kernel");
	i += 2;
	fake_preload[i++] = MODINFO_ADDR;
	fake_preload[i++] = sizeof(vm_offset_t);
	fake_preload[i++] = KERNBASE;
	fake_preload[i++] = MODINFO_SIZE;
	fake_preload[i++] = sizeof(uint32_t);
	fake_preload[i++] = (uint32_t)&end - KERNBASE;
	fake_preload[i++] = MODINFO_NAME;
	fake_preload[i++] = strlen("md root") + 1;
	strcpy((char*)&fake_preload[i++], "md root");
	i += 1;
	fake_preload[i++] = MODINFO_TYPE;
	fake_preload[i++] = strlen("md_image") + 1;
	strcpy((char*)&fake_preload[i++], "md_image");
	i += 2;
	fake_preload[i++] = MODINFO_ADDR;
	fake_preload[i++] = sizeof(uint32_t);
	fake_preload[i++] = MDROOT_ADDR;
	fake_preload[i++] = MODINFO_SIZE;
	fake_preload[i++] = sizeof(uint32_t);
	fake_preload[i++] = MDSIZE * 1024;
	fake_preload[i++] = 0;
	fake_preload[i] = 0;
	preload_metadata = (void *)fake_preload;

	physmem =( 16 * 1024 * 1024) / PAGE_SIZE;
	pc = &__pcpu;
	pcpu_init(pc, 0, sizeof(struct pcpu));
	PCPU_SET(curthread, &thread0);

	physical_start = (vm_offset_t) KERNBASE;
	physical_end =  (vm_offset_t) &end;
	physical_freestart = (((vm_offset_t)physical_end) + PAGE_MASK) & ~PAGE_MASK;
	md_addr.pv_va = md_addr.pv_pa = MDROOT_ADDR;
#define KERNEL_TEXT_BASE (KERNBASE + 0x00040000)
	kerneldatasize = (u_int32_t)&end - (u_int32_t)KERNEL_TEXT_BASE;
	symbolsize = 0;
	freemempos = (vm_offset_t)round_page(physical_freestart);
	memset((void *)freemempos, 0, 256*1024);
		/* Define a macro to simplify memory allocation */
#define	valloc_pages(var, np)			\
	alloc_pages((var).pv_pa, (np));		\
	(var).pv_va = (var).pv_pa;

#define alloc_pages(var, np)			\
	(var) = freemempos;		\
	freemempos += ((np) * PAGE_SIZE);\
	memset((char *)(var), 0, ((np) * PAGE_SIZE));

	while ((freemempos & (L1_TABLE_SIZE - 1)) != 0)
		freemempos += PAGE_SIZE;
	valloc_pages(kernel_l1pt, L1_TABLE_SIZE / PAGE_SIZE);
	valloc_pages(md_bla, L2_TABLE_SIZE / PAGE_SIZE);
	alloc_pages(sa1_cache_clean_addr, CPU_SA110_CACHE_CLEAN_SIZE / PAGE_SIZE);
	for (loop = 0; loop < NUM_KERNEL_PTS; ++loop) {
		valloc_pages(kernel_pt_table[loop],
		    L2_TABLE_SIZE / PAGE_SIZE);
	}

	valloc_pages(systempage, 1);

	/*
	 * Allocate a page for the system page mapped to V0x00000000
	 * This page will just contain the system vectors and can be
	 * shared by all processes.
	 */
	pt_size = round_page(freemempos) - physical_freestart;

	/* Allocate stacks for all modes */
	valloc_pages(irqstack, IRQ_STACK_SIZE);
	valloc_pages(abtstack, ABT_STACK_SIZE);
	valloc_pages(undstack, UND_STACK_SIZE);
	valloc_pages(kernelstack, KSTACK_PAGES);


#ifdef VERBOSE_INIT_ARM
	printf("IRQ stack: p0x%08lx v0x%08lx\n", irqstack.pv_pa,
	    irqstack.pv_va); 
	printf("ABT stack: p0x%08lx v0x%08lx\n", abtstack.pv_pa,
	    abtstack.pv_va); 
	printf("UND stack: p0x%08lx v0x%08lx\n", undstack.pv_pa,
	    undstack.pv_va); 
	printf("SVC stack: p0x%08lx v0x%08lx\n", kernelstack.pv_pa,
	    kernelstack.pv_va); 
#endif
	/*
	 * Allocate memory for the l1 and l2 page tables. The scheme to avoid
	 * wasting memory by allocating the l1pt on the first 16k memory was
	 * taken from NetBSD rpc_machdep.c. NKPT should be greater than 12 for
	 * this to work (which is supposed to be the case).
	 */

	/* Allocate pages for process 0 kernel stack and uarea */
	valloc_pages(proc0_uarea, UAREA_PAGES);
	
	/*
	 * Now we start construction of the L1 page table
	 * We start by mapping the L2 page tables into the L1.
	 * This means that we can replace L1 mappings later on if necessary
	 */
	l1pagetable = kernel_l1pt.pv_pa;


	/* XXX bla **/
#if 0
	bcopy((void*)0xd0300000, &mfs_root, MD_ROOT_SIZE*1024);
#endif
	/* Map the L2 pages tables in the L1 page table */
	pmap_link_l2pt(l1pagetable, 0x00000000,
	    &kernel_pt_table[KERNEL_PT_SYS]);
	pmap_link_l2pt(l1pagetable, KERNBASE,
	    &kernel_pt_table[KERNEL_PT_KERNEL]);
	pmap_link_l2pt(l1pagetable, 0xd0000000,
	    &kernel_pt_table[KERNEL_PT_IO]);
	pmap_link_l2pt(l1pagetable, 0x90000000, &kernel_pt_table[KERNEL_PT_IRQ]);
	pmap_link_l2pt(l1pagetable, MDROOT_ADDR,
	    &md_bla);
	for (loop = 0; loop < KERNEL_PT_VMDATA_NUM; ++loop)
		pmap_link_l2pt(l1pagetable, KERNEL_VM_BASE + loop * 0x00400000,
		    &kernel_pt_table[KERNEL_PT_VMDATA + loop]);
	pmap_map_chunk(l1pagetable, KERNBASE, KERNBASE,
	   (uint32_t)&end - KERNBASE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	/* Map the stack pages */
	pmap_map_chunk(l1pagetable, irqstack.pv_va, irqstack.pv_pa,
	    IRQ_STACK_SIZE * PAGE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, md_addr.pv_va, md_addr.pv_pa,
	    MDSIZE * 1024, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, abtstack.pv_va, abtstack.pv_pa,
	    ABT_STACK_SIZE * PAGE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, undstack.pv_va, undstack.pv_pa,
	    UND_STACK_SIZE * PAGE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, kernelstack.pv_va, kernelstack.pv_pa,
	    KSTACK_PAGES * PAGE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, proc0_uarea.pv_va, proc0_uarea.pv_pa,
	    UAREA_PAGES * PAGE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);


	pmap_map_chunk(l1pagetable, kernel_l1pt.pv_va, kernel_l1pt.pv_pa,
	    L1_TABLE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_PAGETABLE);

	for (loop = 0; loop < NUM_KERNEL_PTS; ++loop) {
		pmap_map_chunk(l1pagetable, kernel_pt_table[loop].pv_va,
		    kernel_pt_table[loop].pv_pa, L2_TABLE_SIZE,
		    VM_PROT_READ|VM_PROT_WRITE, PTE_PAGETABLE);
	}
	pmap_map_chunk(l1pagetable, md_bla.pv_va, md_bla.pv_pa, L2_TABLE_SIZE,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_PAGETABLE);
	/* Map the vector page. */
	pmap_map_entry(l1pagetable, vector_page, systempage.pv_pa,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);
	/* Map SACOM3. */
	pmap_map_entry(l1pagetable, 0xd000d000, 0x80010000, 
	    VM_PROT_READ|VM_PROT_WRITE, PTE_NOCACHE);
	pmap_map_entry(l1pagetable, 0x90050000, 0x90050000,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_NOCACHE);
	pmap_map_chunk(l1pagetable, sa1_cache_clean_addr, 0xf0000000, 
	    CPU_SA110_CACHE_CLEAN_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);

	data_abort_handler_address = (u_int)data_abort_handler;
	prefetch_abort_handler_address = (u_int)prefetch_abort_handler;
	undefined_handler_address = (u_int)undefinedinstruction_bounce;
	undefined_init();
	cpu_domains((DOMAIN_CLIENT << (PMAP_DOMAIN_KERNEL*2)) | DOMAIN_CLIENT);
	setttb(kernel_l1pt.pv_pa);
	cpu_tlb_flushID();
	cpu_domains(DOMAIN_CLIENT << (PMAP_DOMAIN_KERNEL*2));

		/*
	 * Pages were allocated during the secondary bootstrap for the
	 * stacks for different CPU modes.
	 * We must now set the r13 registers in the different CPU modes to
	 * point to these stacks.
	 * Since the ARM stacks use STMFD etc. we must set r13 to the top end
	 * of the stack memory.
	 */
	printf("init subsystems: stacks\n");

	set_stackptr(PSR_IRQ32_MODE,
	    irqstack.pv_va + IRQ_STACK_SIZE * PAGE_SIZE);
	set_stackptr(PSR_ABT32_MODE,
	    abtstack.pv_va + ABT_STACK_SIZE * PAGE_SIZE);
	set_stackptr(PSR_UND32_MODE,
	    undstack.pv_va + UND_STACK_SIZE * PAGE_SIZE);



	/*
	 * We must now clean the cache again....
	 * Cleaning may be done by reading new data to displace any
	 * dirty data in the cache. This will have happened in setttb()
	 * but since we are boot strapping the addresses used for the read
	 * may have just been remapped and thus the cache could be out
	 * of sync. A re-clean after the switch will cure this.
	 * After booting there are no gross reloations of the kernel thus
	 * this problem will not occur after initarm().
	 */
/*	cpu_idcache_wbinv_all();*/


	bootverbose = 1;

#if 0
	set_cpufuncs();
#endif	
	/* Set stack for exception handlers */
	
#if 0
	printf("arm_init: physical_pages = %08x\n", physical_pages);
	printf("arm_init: kernel_l1pt: pa = %08x, va = %08x\n",
	    kernel_l1pt.pv_pa, kernel_l1pt.pv_va);
	printf("arm_init: proc0_uarea: pa = %08x, va = %08x\n",
	    proc0_uarea.pv_pa, proc0_uarea.pv_va);
	printf("arm_init: proc0_kstack: pa = %08x, va = %08x\n",
	    proc0_kstack.pv_pa, proc0_kstack.pv_va);
#endif

/*	printf("arm_init: physfree = %08x\n", physical_freestart);
	printf("arm_init: first = %08x\n", first);
	printf("arm_init: end = %08x\n", (uint32_t) &end);
	
	printf("arm_init: params = %08x\n", params);
	printf("arm_init: params: page_size = %08x\n", params->u1.s.page_size);
	printf("arm_init: params: nrpages = %08x\n", params->u1.s.nr_pages);
	printf("arm_init: params: ramdisk_size = %08x\n", params->u1.s.ramdisk_size);
	printf("arm_init: params: flags = %08x\n", params->u1.s.flags);
	printf("arm_init: params: rootdev = %08x\n", params->u1.s.rootdev);
	printf("arm_init: params: video_num_cols = %08x\n", params->u1.s.video_num_cols);
	printf("arm_init: params: video_num_rows = %08x\n", params->u1.s.video_num_rows);
	printf("arm_init: params: video_x = %08x\n", params->u1.s.video_x);
	printf("arm_init: params: video_y = %08x\n", params->u1.s.video_y);
	printf("arm_init: params: memc_control_reg = %08x\n", params->u1.s.memc_control_reg);
	printf("arm_init: params: sounddefault = %02x\n", params->u1.s.sounddefault);
	printf("arm_init: params: adfsdrives = %02x\n", params->u1.s.adfsdrives);
	printf("arm_init: params: bytes_per_char_h = %02x\n", params->u1.s.bytes_per_char_h);
	printf("arm_init: params: bytes_per_char_v = %02x\n", params->u1.s.bytes_per_char_v);
	for(i = 0; i < 4; i++) {
		printf("arm_init: params: pages_in_bank[%d] = %08x\n", i, params->u1.s.pages_in_bank[i]);
	}
	printf("arm_init: params: pages_in_vram = %08x\n", params->u1.s.pages_in_vram);
	printf("arm_init: params: initrd_start = %08x\n", params->u1.s.initrd_start);
	printf("arm_init: params: initrd_size = %08x\n", params->u1.s.initrd_size);
	printf("arm_init: params: rd_start = %08x\n", params->u1.s.rd_start);
	printf("arm_init: params: system_options = %08x\n", params->u1.s.system_options);
	printf("arm_init: params: system_serial_num = %08x\n", params->u1.s.system_serial_num);
	for(i = 0; i < 8; i++) {
		printf("arm_init: params: paths[%d] = %s\n", i, (params->u2.paths[i][0]) ? params->u2.paths[i] : "(null)");
	}
	printf("arm_init: params: magic = %08x\n", params->u2.s.magic);
	printf("arm_init: params: commandline = %s\n", (params->commandline[0]) ? params->commandline : "(null)");
	printf("arm_init: params: bootsetting = %s\n", (params->bootsetting[0]) ? params->bootsetting : "(null)");*/
	
	proc_linkup(&proc0, &ksegrp0, &kse0, &thread0);
	proc0.p_uarea = (struct user *) proc0_uarea.pv_va;
	thread0.td_kstack = kernelstack.pv_va;
	thread0.td_pcb = (struct pcb *)
		(thread0.td_kstack + KSTACK_PAGES * PAGE_SIZE) - 1;
	thread0.td_pcb->pcb_flags = 0;
	thread0.td_frame = &proc0_tf;
	
	
	/* Enable MMU, I-cache, D-cache, write buffer. */
#if 0
	printf("it was %p\n", (void *)cpufunc_control(0,0));
	printf("ca c fait\n");
	printf("before\n");
	printf("mmu enabled\n");
	printf("now we have %p\n", (void*)cpufunc_control(0,0));
#endif

	cpufunc_control(0x337f, 0x107d);
	got_mmu = 1;
	arm_vector_init(ARM_VECTORS_LOW, ARM_VEC_ALL);


	pmap_bootstrap(KERNEL_VM_BASE, 
	    KERNEL_VM_BASE + KERNEL_PT_VMDATA_NUM * 0x400000, &kernel_l1pt);

	
	mutex_init();
	
	
#if 0
	phys_avail[0] = 0x00000000;
	phys_avail[1] = physmem;
	phys_avail[2] = 0;
#endif
#if 0
	phys_avail[1] = physical_start;
	phys_avail[2] = physical_freestart;
	phys_avail[3] = physmem;
#endif
#if 0
	phys_avail[3] = 0;
#endif
#if 0
	phys_avail[1] = 0x01000000 - 1;
#endif
	phys_avail[0] = round_page(freemempos);
	phys_avail[1] = 0xc0000000 + 0x02000000 - 1;
	phys_avail[2] = 0;
	phys_avail[3] = 0;
#if 0
	phys_avail[4] = 0x00000000;
	phys_avail[5] = 0x00000000;
#endif
	
	/* Do basic tuning, hz etc */
	init_param1();
	init_param2(physmem);
	printf("arm_init: done!\n");
	avail_end = 0xc0000000 + 0x02000000 - 1;
	return ((void *)(kernelstack.pv_va + USPACE_SVC_STACK_TOP));
}
