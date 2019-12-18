/*
 * Intel provides this code “as-is” and disclaims all express and implied warranties, including without 
 * limitation, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement, 
 * as well as any warranty arising from course of performance, course of dealing, or usage in trade. No license 
 * (express or implied, by estoppel or otherwise) to any intellectual property rights is granted by Intel providing 
 * this code. 
 * This code is preliminary, may contain errors and is subject to change without notice. 
 * Intel technologies' features and benefits depend on system configuration and may require enabled hardware, 
 * software or service activation. Performance varies depending on system configuration.  Any differences in your 
 * system hardware, software or configuration may affect your actual performance.  No product or component can be 
 * absolutely secure.  
 * Intel and the Intel logo are trademarks of Intel Corporation in the United States and other countries. 
 * *Other names and brands may be claimed as the property of others.
 * © Intel Corporation
 */

/*
 * Licensed under the GPL v2
 */

#include "gcc-common.h"
#include "postvartrackpass.h"

/* Randpoline jmp sled quantity */
#define RANDPOLINE_SIZE  64000

/* Insert INT3 after each ret */
unsigned int INSERT_INT3 = false;
unsigned int INSERT_UD2 = true;
unsigned int COMPILETIMERAND = true;

/* bitmap to control register allocation for randpoline */
unsigned int g_randpoline_reg_map = 0;

/* Number of locations patched and missing patch (that should have it */
unsigned int g_fixes = 0;
unsigned int g_misses = 0;

/* GPL v2 */
int plugin_is_GPL_compatible;

/* Helper must be defined, but we do not use it */
template <>
template <>
inline bool
is_a_helper <rtx_insn *>::test (rtx_insn *p) {
	return 0;
}


/* Returns the local date/time formatted as 2019-01-01 11:11:42 - used in the _LOG_ macro */
char* getFormattedTime(void) {
	time_t rawtime;
	struct tm* timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	/* Must be static, otherwise won't work */
	static char _retval[20];
	strftime(_retval, sizeof(_retval), "%Y-%m-%d %H:%M:%S", timeinfo);

	return _retval;
}

/* The gen_push or gen_pop generate push/pop RTXs for a given reg number */
static rtx gen_push(const rtx the_reg) {
	return gen_rtx_SET(
			gen_rtx_MEM(DImode, gen_rtx_PRE_DEC(DImode, stack_pointer_rtx)), 
			the_reg);
}


/* The gen_int3 generate an int3 instruction */
static inline void gen_int3(const rtx insn) {
	rtx as = gen_rtx_ASM_OPERANDS(VOIDmode,
				"int3\n\t",
                "",
                0,
                rtvec_alloc(0),
                rtvec_alloc(0),
                rtvec_alloc(0),
                expand_location(RTL_LOCATION(insn)).line);
	emit_insn_after(as, insn);
}


/* Depending on the reg used for randpoline, the size of the jump instructions vary (64-bits only) */
static unsigned int get_gap_size(const int regno) {
	unsigned int size = 2; 
	
	if (regno >= R8_REG) {
		size = 3;
	}
	
	if (INSERT_UD2 == true) {
		size += 2;
	}
	
	return size;
}


/* Verifies whether a JUMP_INSN has an indirect target
 * returns the PATTERN of the JUMP_INSN or respectively of the nested INSN
 */
static const rtx gimme_indirect_jump(rtx expr, const rtx insn) {
	
	if (GET_CODE(expr) == PARALLEL) {
		expr = XVECEXP(expr, 0, 0);
	}
	
	/* patching every return with a follow up int3 if INSERT_INT3 is enabled */
	if (ANY_RETURN_P(expr)) {		
		if (INSERT_INT3) {
			gen_int3(insn);
		}
		return NULL;
	}
	
	/* target of a given JUMP is either MEM or a REG */
	if (GET_CODE(expr) == SET && (MEM_P(XEXP(expr, 1)) || REG_P(XEXP(expr, 1)))) {
		return expr;
	}

	return NULL;

}


/* Verifies whether a CALL_INSN has an indirect target
 * returns the PATTERN of the CALL_INSN or respectively of the nested INSN
 */
static const rtx gimme_indirect_call(rtx expr) {

	if (GET_CODE(expr) == SET) {
		expr = XEXP(expr, 1);
	}
	
	/* target of given CALL is not a constant RTX object class */
	if (GET_CODE(expr) == CALL && GET_RTX_CLASS(GET_CODE(XEXP(XEXP(expr,0),0))) != RTX_CONST_OBJ) {
		return expr;
	}
	return NULL;
}


/* Generates a name for a given thunk, based on which register or mem is handled */
static const char* randpoline_thunkname (unsigned int regno) {
	char reg_prefix;
	
	char name[30];
	memset(name, 0, 30);
	
	if (regno != INVALID_REGNUM) {
		if (LEGACY_INT_REGNO_P(regno)) {
			reg_prefix = TARGET_64BIT ? 'r' : 'e';
		} else {
			reg_prefix = '_';
		}
		
		sprintf(name, "randpoline_thunk_%c%s", reg_prefix, reg_names[regno]);
	}
	else {
		sprintf(name, "randpoline_thunk");
	}

	/* allocates for a given string in the GGC managed memory */
	return ggc_alloc_string(name, -1);
		
}


/* Outputs randpolines for a given register number */
static void output_randpoline (unsigned int regno) { 
	const char *thunkname, *insntemplate;
	rtx xops[1];
	unsigned int i;

	thunkname = randpoline_thunkname(regno);

	switch_to_section(text_section);
	ASM_OUTPUT_LABEL(asm_out_file, thunkname);
	
	xops[0] = gen_rtx_REG (word_mode, regno);
	
	insntemplate = "jmp\t*%0; ud2";
	if (INSERT_UD2 == false) {
		insntemplate = "jmp\t*%0";
	}
	
	for (i = 0; i < RANDPOLINE_SIZE; i++) { 
		output_asm_insn (insntemplate, xops);
	}
	
	switch_to_section(current_function_section());
}


/* RAND initiliazation function for runtime init */
static void output_randinit (void) {
	
	rtx xops[1], xaps[1], xips[1];    
	
	/*
	 * calculate random value covering 50% of randpoline, added at runtime to 
	 * static value covering other 50%, stored in r13 
	 * 
	 * rdrand
	 * call srand => r13
	 * r13 mod RANDPOLINE_SIZE / 2
	 */
	 
	switch_to_section(text_section);
	
	ASM_OUTPUT_LABEL(asm_out_file, "randinit");

	xops[0] = gen_rtx_REG(DImode, AX_REG);
	xaps[0] = gen_rtx_REG(DImode, CX_REG);
	xips[0] = gen_rtx_REG(DImode, DX_REG);
	
	output_asm_insn("push\t%0", xops);
	output_asm_insn("push\t%0", xaps);
	output_asm_insn("push\t%0", xips);
	
	fprintf(asm_out_file, "\trdrand\t%%eax\n\tcall\tsrand\n\trdrand\t%%r13\n");
	
	/*
	 * mov rcx, 32000
	 * mov rax, r13
	 * cdq
	 * idiv rcx
	 * mov r13, rdx
	 */
	
	fprintf(asm_out_file, "\tmov\t$%d, %%ecx\n", RANDPOLINE_SIZE / 2);
	fprintf(asm_out_file, "\tmov\t%%r13, %%rax\n\txor\t%%rdx, %%rdx\n\tidiv\t%%ecx\n\tmov\t%%rdx, %%r13\n");
	
	output_asm_insn("pop\t%0", xips);
	output_asm_insn("pop\t%0", xaps);
	output_asm_insn("pop\t%0", xops);
	fprintf(asm_out_file, "\tret\n");
	
	switch_to_section(current_function_section());
}


/* Tests whether a register is being used within the basic block after the current indirection
 * TODO rewrite to see whether its being read, we don't care about writing
 */
static int reg_used_after_ind(int regno, basic_block bb, const rtx ind_insn) {
	rtx test_reg = gen_rtx_REG(DImode, regno);
	rtx_insn *insn;
	
	for (insn = (rtx_insn*)ind_insn; insn && insn != NEXT_INSN(BB_END(bb)); insn = NEXT_INSN(insn)) {
		/* TODO specific test setup for this case */
		if (reg_overlap_mentioned_p(test_reg, insn) || reg_referenced_p(test_reg, insn)) {
			return 1;
		}
	}
	return 0;
}


/* Test if register is live in current bitmap, bitmap being either bb_in or bb_out
 * for JUMP, the jmps are the end of the BB (?), if unconditional
 * it is assumed, that if a register wants to live in the next block, its content shall not be changed
 */
static int reg_live_on_blockboundary(int regno, bitmap liveness) {
	if (REGNO_REG_SET_P(liveness, regno)) {
		return 1;
	}
	return 0;
}


/* Check the fixed registers and non call used regs that are clean, whether they are taken
 * care of in pro/epilogue
 */
static int reg_already_callee_saved(const unsigned int regno) {
	rtx_insn *iter;
	rtx push_reg = gen_push(gen_rtx_REG(DImode, regno));
	basic_block bb;
	
	FOR_EACH_BB_FN(bb, cfun) {
		FOR_BB_INSNS(bb, iter) {
			if (NOTE_P(iter) && NOTE_KIND(iter) == NOTE_INSN_PROLOGUE_END) {
				return 0;
			}

			/* check whether register already handled in prologue */
			if (INSN_P(iter) && GET_CODE(PATTERN(iter)) == SET) {
				if (rtx_equal_p(PATTERN(iter), push_reg)) {
					return 1;
				}
			}
		}
	}
	
	return 0;
}


/* Retrieves a REG insn not live on block exit and not used within block after indirection
 * exclude: scratch and randpoline_reg shall not be the same
 */
static rtx get_scratch_register(basic_block bb, const rtx ind_insn, const int exclude) {
	bitmap liveout = df_get_live_out(bb);
	int regno;
	
	/* Important to note: jmp to any register higher than r8 produces 3 byte jmp instructions */
	static int const scratchregisters[13] = {R10_REG, R11_REG, DX_REG, CX_REG, BX_REG, AX_REG, 
								SI_REG, DI_REG, R8_REG, R9_REG, R12_REG, R14_REG, R15_REG};

	for (int i = 0; i < 13; i++) {
		regno = scratchregisters[i];
			
		if (!reg_live_on_blockboundary(regno, liveout) && 
			!reg_used_after_ind(regno, bb, ind_insn) &&
			regno != exclude) {
	
			if (fixed_regs[regno] || !call_used_regs[regno]) {
				if (!reg_already_callee_saved(regno)) {
					continue;
				}
			}
			return gen_rtx_REG(DImode, regno);
		}
	}
	
	/* none of the wanted regs is available, no scratch reg */
	return NULL;
}


/* Replaces a call that was identified as indirect with a call insn pointing to the randpoline code */
static void instrument_indirect_call(const rtx insn, const rtx indirection, unsigned int target, 
			rtx randpoline_reg, rtx scratch_register) {
	
	rtx target_rtx, mov_target_randpol, lea_retpol, symbol_thunkname, no_scratch_reg; 
	rtx multiply_r13, lea_r13, myr13, add_both;
	unsigned int myrand, jumpinsn_size;
	
	const char *thunkname; 
	
	
	thunkname = randpoline_thunkname(REGNO(randpoline_reg));
	jumpinsn_size = get_gap_size(REGNO(randpoline_reg));
	myrand = (rand() % (RANDPOLINE_SIZE / 2) ) * jumpinsn_size;
	myr13 = gen_rtx_REG(DImode, R13_REG);
	
	/* CALL insn stub:
	 *	 lea RANDPOLINE_REG, [r13 * jmpinsnsize]
	 *   lea SCRATCH, [randpoline + rand * jmpinsnsize]
	 *	 add SCRATCH, RANDPOLINE_REG
	 *   mov RANDPOLINE_REG, target
	 *   call SCRATCH 
	 */
	 
	/* Alternative stub, if no scratch register available (note, no re-randomization possible):
	 * mov RANDPOLINE_REG, target
	 * call [calculated_offset_in_randpoline]
	 */
	
	/* CALL insn specific */
	if (target == INVALID_REGNUM) {
		target_rtx = XEXP(XEXP(indirection,0),0);
	} else {
		target_rtx = gen_rtx_REG(DImode, target);
	}
	
	/* the respective randpoline symbol */
	symbol_thunkname = gen_rtx_SYMBOL_REF(Pmode, thunkname);
	SYMBOL_REF_FLAGS(symbol_thunkname) |= SYMBOL_FLAG_LOCAL;
	
	if (scratch_register != NULL) {
		
		/* LEA only accepts factors 1,2,4,8 hence for 5 byte jmp we calc r13*4+r13 */
		if (jumpinsn_size % 2 == 0) {
			multiply_r13 = gen_rtx_MULT(Pmode, myr13, gen_rtx_CONST_INT(DImode, jumpinsn_size));
		} else {
			jumpinsn_size--;
			multiply_r13 = gen_rtx_PLUS(Pmode, 
				gen_rtx_MULT(Pmode, myr13, gen_rtx_CONST_INT(DImode, jumpinsn_size)),
				myr13);
		}
		
		lea_r13 = gen_rtx_SET(
			randpoline_reg, 
			multiply_r13
		);
		
		/* lea RANDPOLINE_REG, [r13 * jmpinsnsize] */
		emit_insn_before(lea_r13, insn);
		
		lea_retpol = gen_rtx_SET(scratch_register, 
			plus_constant(Pmode, symbol_thunkname, myrand)
		);
		
		/* lea SCRATCH, [randpoline + rand * jmpinsnsize] */
		emit_insn_before(lea_retpol, insn);
		
		add_both = gen_rtx_SET(scratch_register, 
			gen_rtx_PLUS(DImode, scratch_register, randpoline_reg)
		);
		
		/* add SCRATCH, RANDPOLINE_REG */
		emit_insn_before(add_both, insn);
		
		
		/*
		XORing with random val in r13 to runtime-randomize randpoline target
		xor_r13 = gen_rtx_PARALLEL( VOIDmode,
			gen_rtvec(2, 
				gen_rtx_SET(scratch_register, 
					gen_rtx_XOR(Pmode, scratch_register, gen_rtx_REG(DImode, R13_REG))),
				gen_rtx_CLOBBER(VOIDmode, gen_rtx_REG(CCmode, FLAGS_REG))
			)
		);
			
		emit_insn_before(xor_r13, insn);
		*/
	
		/* replacing target of former call insn */
		XEXP(XEXP(indirection,0),0) = scratch_register;
		
	} else {
		no_scratch_reg = force_const_mem(Pmode, plus_constant(Pmode, symbol_thunkname, myrand));
		XEXP(XEXP(indirection,0),0) = no_scratch_reg;
	}
		
	/* mov RANDPOLINE_REG, target_rtx */
	mov_target_randpol = gen_rtx_SET(randpoline_reg, target_rtx);
	emit_insn_before(mov_target_randpol, insn); 


}


/* replace indirect jmp insns with randpoline stub */
static void instrument_indirect_jmp(rtx insn, rtx indirection, unsigned int target, 
			rtx randpoline_reg, rtx scratch_register) {
	rtx target_rtx, mov_target_randpol, lea_retpol, symbol_thunkname, no_scratch_reg; 
	rtx multiply_r13, lea_r13, myr13, add_both;
	unsigned int myrand, jumpinsn_size;
	
	const char *thunkname; 
	
	thunkname = randpoline_thunkname(REGNO(randpoline_reg));
	jumpinsn_size = get_gap_size(REGNO(randpoline_reg));
	myrand = (rand() % (RANDPOLINE_SIZE / 2)) * jumpinsn_size;
	myr13 = gen_rtx_REG(DImode, R13_REG);
	
	
	/* TODO check memmodes for 32/64 bits, for 32 possible problem in integer constant myrand */

	/* JMP insn stub:
	 *
	 * lea RANDPOLINE_REG, [r13 * jumpinsnsize]
	 * lea SCRATCH, [randpoline + rand * jumpinsnsize]
	 * add SCRATCH, RANDPOLINE_REG
	 * mov RANDPOLINE_REG, target_rtx
	 * jmp SCRATCH
	 */
	
	
	/* If the target is indirect mem, then target_rtx is the actual target, otherwise rtx register */
	if (target == INVALID_REGNUM) {
		target_rtx = XEXP(indirection,1);
	} 
	else {
		target_rtx = gen_rtx_REG(DImode, target);
	}
	
	/* lea SCRATCH, [randpoline + rand * x]  => rand mod 64000 (in which 64000 is RANDPOLINE_SIZE) */
	symbol_thunkname = gen_rtx_SYMBOL_REF(Pmode, thunkname);
	SYMBOL_REF_FLAGS(symbol_thunkname) |= SYMBOL_FLAG_LOCAL;
	
	if (scratch_register != NULL) {
		
		/* LEA only accepts factors 1,2,4,8 hence for 5 byte jmp we calc r13*4+r13 */
		if (jumpinsn_size % 2 == 0) {
			multiply_r13 = gen_rtx_MULT(Pmode, myr13, gen_rtx_CONST_INT(DImode, jumpinsn_size));
		} else {
			jumpinsn_size--;
			multiply_r13 = gen_rtx_PLUS(Pmode, 
				gen_rtx_MULT(Pmode, myr13, gen_rtx_CONST_INT(DImode, jumpinsn_size)),
				myr13);
		}
		
		lea_r13 = gen_rtx_SET(
			randpoline_reg, 
			multiply_r13
		);
		
		/* lea RANDPOLINE_REG, [r13 * jmpinsnsize] */
		emit_insn_before(lea_r13, insn);
		
		lea_retpol = gen_rtx_SET(scratch_register, 
			plus_constant(Pmode, symbol_thunkname, myrand)
		);
		
		/* lea SCRATCH, [randpoline + rand * jmpinsnsize] */
		emit_insn_before(lea_retpol, insn);
		
		add_both = gen_rtx_SET(scratch_register, 
			gen_rtx_PLUS(DImode, scratch_register, randpoline_reg)
		);
		
		/* add SCRATCH, RANDPOLINE_REG */
		emit_insn_before(add_both, insn);
		
		XEXP(indirection,1) = scratch_register;  
		
	} else {
		/* randpoline address hardcoded as memory address, no re-randomization feasible */
		no_scratch_reg = force_const_mem(Pmode, plus_constant(Pmode, symbol_thunkname, myrand));
		indirection = gen_rtx_SET(pc_rtx, no_scratch_reg); 
	}		
	
	/* mov RANDPOLINE_REG, target_rtx */
	mov_target_randpol = gen_rtx_SET(randpoline_reg, target_rtx);
	emit_insn_before(mov_target_randpol, insn); 
}


/* Checks insns for indirect branches */
static rtx gimme_indirection(const rtx insn) {
	if (GET_CODE(insn) == CALL_INSN) {
		return(gimme_indirect_call(PATTERN(insn)));
	}
	if (GET_CODE(insn) == JUMP_INSN) {
		return(gimme_indirect_jump(PATTERN(insn), insn));
	}
	return NULL;
}


/* Retrieves the target part of an indirect branch */
static unsigned int gimme_target(const rtx insn, const rtx indirection) {
	unsigned int target = 0;
	
	if (GET_CODE(insn) == CALL_INSN) {
		if (MEM_P(XEXP(XEXP(indirection,0),0))) {
			target = INVALID_REGNUM;
		} else if (REG_P(XEXP(XEXP(indirection,0),0))) {
			target = REGNO(XEXP(XEXP(indirection,0),0));
		} else {
			gcc_unreachable();
		}
	}
		
	if (GET_CODE(insn) == JUMP_INSN) {
		if (MEM_P(XEXP(indirection, 1))) {
			target = INVALID_REGNUM;
		} else if (REG_P(XEXP(indirection, 1))) {
			target = REGNO(XEXP(indirection, 1));
		} else {
			gcc_unreachable();
		}
	}
		
	return target;	
}


/* Pass execution for randpoline after vartrack pass */
static unsigned int randpoline_execute(void) {
	basic_block bb;
	rtx indirection, randpoline_reg, scratch_register; 
	rtx_insn *insn;
	unsigned int target = 0;
	int regno_randpoline_reg;
			
	
	FOR_EACH_BB_FN(bb, cfun) {
		/* each basic block with indirections needs to verify its own scratch reg */
		scratch_register = NULL;
		randpoline_reg = NULL;
		
		FOR_BB_INSNS(bb, insn) {

			indirection = gimme_indirection(insn);
			
			/* fixing up the indirect branches
			 * we'll need two scratch registers, one of which has an associated randpoline
			 * RANDPOLINE_REG can't be target, or live on block exit, or used within block after ind
			 * but, target is live in current insn anyway
			 */
			if (indirection != NULL) {
				target = gimme_target(insn, indirection);
						
				/* scratch_register and randpoline_reg can remain the same for one basic block, 
				 * if evaluated once. for randpoline_reg, if no actual randpoline present yet, 
				 * gotta create one
				 */
				if (randpoline_reg == NULL) {
					randpoline_reg = get_scratch_register(bb, insn, -1);
				}

				if (randpoline_reg != NULL) {
					
					regno_randpoline_reg = REGNO(randpoline_reg);

					if (!scratch_register) {
						scratch_register = get_scratch_register(bb, insn, regno_randpoline_reg);
						
						if (SIBLING_CALL_P(insn) && scratch_register) {
							int temp = REGNO(scratch_register);
							if (temp != 0 && temp != R10_REG && temp != R11_REG) {
								scratch_register = NULL;
							}
						}
					}
					
					/* ix86_indirect_branch_register indicates that we cannot indirectly jump to mem, hence
						we have to have a scratch_register */
					if ( (ix86_indirect_branch_register && scratch_register) || 
						(!ix86_indirect_branch_register) ) {

						
						/* all_randpolines registers for which register we already created a randpoline */
						if (!(g_randpoline_reg_map & (1 << regno_randpoline_reg))) {
							output_randpoline(regno_randpoline_reg);
							g_randpoline_reg_map |= 1 << regno_randpoline_reg;
						}
					
						/* finally, perform the randpoline insertion */
						if (GET_CODE(insn) == CALL_INSN) {
							instrument_indirect_call(insn, indirection, target, randpoline_reg, 
								scratch_register);
						} else {
							instrument_indirect_jmp(insn, indirection, target, randpoline_reg, 
								scratch_register);
						}
						
						g_fixes++;
						
					} else {
						location_t loc = RTL_LOCATION(insn);
						warning_at(loc, 0, "Function %s insn %d not patchable, scratch register missing", 
							IDENTIFIER_POINTER(DECL_NAME(cfun->decl)), INSN_UID(insn));
						g_misses++;
					}
					
				} else {
					location_t loc = RTL_LOCATION(insn);
					warning_at(loc, 0, "Function %s insn %d not patchable, randpoline and scratch register missing", 
						IDENTIFIER_POINTER(DECL_NAME(cfun->decl)), INSN_UID(insn));
					g_misses++;
				}
			}
		}
		
	}
	
			
	return 0;
}


#define PASS_NAME randpoline
#define NO_GATE
#define PROPERTIES_REQUIRED PROP_rtl
#include "gcc-generate-rtl-pass.h"

/* Cleanup and logging */
static void cleanup_routine (void *gcc_data, void *user_data) {
	
	LOGINFO("\nSTATS -- g_fixes: %d -- g_misses: %d\n", g_fixes, g_misses);
	
	return;
}

/* Todo see whether we can build the randinit somewhere more suitable  */
static void init_routine (void *gcc_data, void *user_data) {
	
	if (COMPILETIMERAND) {
		
		/* we need stdlib to build */
		
		/* in randinit store rand num in r13 */
		/* TODO gotta deal with inline asm */
		/* write the randinit function to .text section */
		output_randinit();
						 
		/* add randinit to the .init_array */
		default_elf_init_array_asm_out_constructor(gen_rtx_SYMBOL_REF(Pmode, "randinit"), 
			DEFAULT_INIT_PRIORITY);
		
		COMPILETIMERAND = false;
	}
	
	
	
}

static struct plugin_info superplugin_plugin_info = {
	.version = "1.42",
	.help	 = "Do not look very helpful\n",
};


/* PLUGIN INIT */
int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version) {
	
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
  
	int i, myseed;
  
	if (!plugin_default_version_check(version, &gcc_version))
		return 1;


	/* Plugin argument parsing */

	for (i = 0; i < argc; i++) {
		
		if (strcmp(argv[i].key, "int3") == 0) {
			if (strcmp(argv[i].value, "on") == 0) {
				INSERT_INT3 = true;
			}
			continue;
		}
		
		if (strcmp(argv[i].key, "ud2") == 0) {
			if (strcmp(argv[i].value, "off") == 0) {
				INSERT_UD2 = false;
			}				
			continue;
		}
		
		if (strcmp(argv[i].key, "runtime-rand") == 0) {
			if (strcmp(argv[i].value, "off") == 0) {
				COMPILETIMERAND = false;
			}				
			continue;
		}
		
		LOGINFO("\nInvalid parameter %s handed to plugin %s", argv[i].key, plugin_name);
		exit(-1);
	}

	if (!COMPILETIMERAND) {
		/* if -frandom-seed= is provided, GCC will set_random_seed the seed to the provided 
		value (opts-global.c) */
	
		myseed = get_random_seed(false);
		srand(myseed);
	}
	
	fix_register("r13", 1, 1);

	/* PLUGIN INFOS */

	/* vartrack is the last RTL pass with cfg info available */
	PASS_INFO(randpoline, "vartrack", 1, PASS_POS_INSERT_AFTER);
  
	/* CALLBACKS */
	register_callback(plugin_name, PLUGIN_ALL_PASSES_START, &init_routine, NULL);
	register_callback(plugin_name, PLUGIN_INFO, NULL, &superplugin_plugin_info);
	register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &randpoline_pass_info);
  
	/* cleanup of allocated memory */
	register_callback(plugin_name, PLUGIN_FINISH, &cleanup_routine, NULL);
  
	return 0;
}
