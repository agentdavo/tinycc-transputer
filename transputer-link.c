#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET EM_TRANSPUTER

#define R_DATA_32  R_TRANSPUTER_32
#define R_DATA_PTR R_TRANSPUTER_64
#define R_JMP_SLOT R_TRANSPUTER_JUMP_SLOT
#define R_GLOB_DAT R_TRANSPUTER_64
#define R_COPY     R_TRANSPUTER_COPY
#define R_RELATIVE R_TRANSPUTER_RELATIVE

#define R_NUM      R_TRANSPUTER_NUM

#define ELF_START_ADDR 0x00010000
#define ELF_PAGE_SIZE 0x1000

#define PCRELATIVE_DLLPLT 1
#define RELOCATE_DLLPLT 1

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"

/* Returns 1 for a code relocation, 0 for a data relocation. For unknown
   relocations, returns -1. */
int code_reloc (int reloc_type)
{
     switch (reloc_type) {
		case R_TRANSPUTER_IPREL32:
		case R_TRANSPUTER_IPREL16:
		case R_TRANSPUTER_IPREL8:
		case R_TRANSPUTER_16:
        case R_TRANSPUTER_32:
		case R_TRANSPUTER_GOTPC32:
		case R_TRANSPUTER_GOTPC16:
		case R_TRANSPUTER_GOTOFF32:
		case R_TRANSPUTER_GOTOFF16:
		case R_TRANSPUTER_GOT32:
		case R_TRANSPUTER_GOT16:
		case R_TRANSPUTER_GLOB_DAT:
		case R_TRANSPUTER_COPY:
            return 0;
		case R_TRANSPUTER_PLT16:
		case R_TRANSPUTER_PLT32:
		case R_TRANSPUTER_JMP_SLOT:
            return 1;
    }

    //tcc_error ("Unknown relocation type: %d", reloc_type);
    return -1;
}

/* Returns an enumerator to describe whether and when the relocation needs a
   GOT and/or PLT entry to be created. See tcc.h for a description of the
   different values. */
int gotplt_entry_type (int reloc_type)
{
    switch (reloc_type) {
		case R_TRANSPUTER_GLOB_DAT:
     	case R_TRANSPUTER_JMP_SLOT:
        	case R_TRANSPUTER_COPY:
        	case R_TRANSPUTER_RELATIVE:
            return NO_GOTPLT_ENTRY;

		/* 	The following relocs wouldn't normally need GOT or PLT
	   		slots, but we need them for simplicity in the link
	   		editor part.  See our caller for comments.  */
        	case R_TRANSPUTER_32:
        	case R_TRANSPUTER_16:
        	case R_TRANSPUTER_8:
            return AUTO_GOTPLT_ENTRY;

		case R_TRANSPUTER_GOTOFF16:
            return BUILD_GOT_ONLY;

        	case R_TRANSPUTER_GOT32:
        	case R_TRANSPUTER_GOT16:
        	case R_TRANSPUTER_GOTPC32:
        	case R_TRANSPUTER_GOTPC16:
        	case R_TRANSPUTER_GOTOFF32:
        	case R_TRANSPUTER_PLT32:
        	case R_TRANSPUTER_PLT16:
            return ALWAYS_GOTPLT_ENTRY;
    }

    //tcc_error ("Unknown relocation type: %d", reloc_type);
    return -1;
}

/* 	GOT (Global offset table) = Linker Table for data addresses needed for realocation from library
 *	PLT (Procedure linkage table) = Linker Table for external functions (procedure calls)
 *	 							addresses from library
 */

ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
    	Section *plt = s1->plt;
    	uint8_t *p;
    	unsigned plt_offset;

    	if (s1->output_type == TCC_OUTPUT_DLL)
        tcc_error("DLLs unimplemented!");

	if (plt->data_offset == 0) {
		p = section_ptr_add(plt, 16);
		p[0] = 0x80 | PTR_SIZE;
		write32le(p+4, 0x25FA);
     	p[8] = 0x80 | PTR_SIZE*2;
        	write32le(p+12, 0xF6);
	}

	plt_offset = plt->data_offset;

	relofs = s1->got->reloc ? s1->got->reloc->data_offset : 0;
	rel_size = relofs / sizeof (ElfW_Rel);

    	/* Jump to GOT entry where ld.so initially put the address of ip + 4 */
    	p = section_ptr_add(plt, 16);
	write32le(p, 0x2080 | (got_offset >> 4) << 8 | (0x0F & got_offset));
	p[4] = 0xF6;
	write32le(p+8, 0x2040 | (rel_size >> 4) << 8 | (0x0F & rel_size));
	write32le(p+12, 0x25FA);
	write32le(p+16, 0x6000 | (plt_offset >> 4) << 8 | (0x0F & plt_offset));
	
	return plt_offset;
}

/* relocate the PLT: compute addresses and offsets in the PLT now that final
   address for PLT and GOT are known (see fill_program_header) */
ST_FUNC void relocate_plt(TCCState *s1)
{
    uint8_t *p, *p_end;

    if (!s1->plt)
      return;

    p = s1->plt->data;
    p_end = p + s1->plt->data_offset;

    if (p < p_end) {
        uint64_t plt = s1->plt->sh_addr;
        uint64_t got = s1->got->sh_addr;
        uint64_t off = (got >> 12) - (plt >> 12);
        if ((off + ((uint32_t)1 << 20)) >> 21)
            tcc_error("Failed relocating PLT (off=0x%lx, got=0x%lx, plt=0x%lx)", off, got, plt);
        write32le(p, 0x0);
        write32le(p + 4, 0x0);
        write32le(p + 8, 0x0);
        write32le(p + 12, 0x0);
        write32le(p + 16, 0x0);
        write32le(p + 20, 0x0);
        write32le(p + 24, 0x0);
        write32le(p + 28, 0x0);
        p += 32;
        while (p < p_end) {
            uint64_t pc = plt + (p - s1->plt->data);
            uint64_t addr = got + read64le(p);
            uint64_t off = (addr >> 12) - (pc >> 12);
            if ((off + ((uint32_t)1 << 20)) >> 21)
                tcc_error("Failed relocating PLT (off=0x%lx, addr=0x%lx, pc=0x%lx)", off, addr, pc);
            write32le(p, 0x0);
            write32le(p + 4, 0x0);
            write32le(p + 8, 0x0);
            write32le(p + 12, 0x0);
            p += 16;
        }
    }
}

void relocate_init(Section *sr) {}

void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr, addr_t addr, addr_t val)
{
    int sym_index = ELFW(R_SYM)(rel->r_info);
#ifdef DEBUG_RELOC
    ElfW(Sym) *sym = &((ElfW(Sym) *)symtab_section->data)[sym_index];
#endif

    switch(type) {
        default:
            fprintf(stderr, "FIXME: handle reloc type %x at %x [%p] to %x\n",
                    type, (unsigned)addr, ptr, (unsigned)val);
            return;
    }
}
#endif
